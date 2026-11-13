package trackparse

import (
	"errors"
	"math"
	"strconv"
	"unicode/utf8"
)

// JSON for the module: a writer whose bytes equal JavaScript's JSON.stringify for every output
// type, MarshalJSON / UnmarshalJSON on the public types, and a small JSON value with a reader
// for the internal JSON door (internal/wire) and the fixture tests. The module has no
// dependency, and encoding/json would not give the same bytes: it escapes <, >, & and
// U+2028/U+2029. (Called through encoding/json, MarshalJSON output is still re-escaped that way
// unless the encoder has SetEscapeHTML(false); the values are the same, the bytes are not.)

// ---------------------------------------------------------------------------------------------
// Writer

// appendJSString appends s as JSON.stringify writes a string: only ", \ and the code points
// below U+0020 are escaped, with the short forms \b \f \n \r \t. Bytes that are not valid
// UTF-8 are replaced as the API replaces them (sanitize: one U+FFFD per maximal ill-formed
// subpart), so a typed Edit with bad bytes reads as the same text as a bad input string.
func appendJSString(b []byte, s string) []byte {
	s = sanitize(s)
	b = append(b, '"')
	for _, r := range s {
		switch r {
		case '"':
			b = append(b, '\\', '"')
		case '\\':
			b = append(b, '\\', '\\')
		case '\n':
			b = append(b, '\\', 'n')
		case '\r':
			b = append(b, '\\', 'r')
		case '\t':
			b = append(b, '\\', 't')
		case '\b':
			b = append(b, '\\', 'b')
		case '\f':
			b = append(b, '\\', 'f')
		default:
			if r < 0x20 {
				const hex = "0123456789abcdef"
				b = append(b, '\\', 'u', '0', '0', hex[r>>4], hex[r&0xf])
			} else {
				b = utf8.AppendRune(b, r)
			}
		}
	}
	return append(b, '"')
}

// appendJSNumber appends n as JavaScript's Number#toString writes it (ECMA-262
// Number::toString): the shortest digits that round-trip, no ".0" on whole values, exponent
// form from 1e21 and below 1e-6 ("1e+21", "1e-7"), and -0 as 0. NaN and the infinities are
// written as null, as JSON.stringify does.
func appendJSNumber(b []byte, n float64) []byte {
	if math.IsNaN(n) || math.IsInf(n, 0) {
		return append(b, "null"...)
	}
	if n == 0 {
		return append(b, '0')
	}
	if n < 0 {
		b = append(b, '-')
		n = -n
	}
	// 'e' with precision -1 gives the shortest round-trip digits as d[.ddd]e±XX.
	var buf [32]byte
	sci := strconv.AppendFloat(buf[:0], n, 'e', -1, 64)
	var digits []byte
	exp := 0
	for i := 0; i < len(sci); i++ {
		c := sci[i]
		if c == 'e' {
			e, err := strconv.Atoi(string(sci[i+1:]))
			if err == nil {
				exp = e
			}
			break
		}
		if c >= '0' && c <= '9' {
			digits = append(digits, c)
		}
	}
	k := len(digits)
	// The value is 0.digits times 10^point.
	point := exp + 1
	switch {
	case k <= point && point <= 21:
		b = append(b, digits...)
		for i := 0; i < point-k; i++ {
			b = append(b, '0')
		}
	case 0 < point && point <= 21:
		b = append(b, digits[:point]...)
		b = append(b, '.')
		b = append(b, digits[point:]...)
	case -6 < point && point <= 0:
		b = append(b, '0', '.')
		for i := 0; i < -point; i++ {
			b = append(b, '0')
		}
		b = append(b, digits...)
	default:
		b = append(b, digits[0])
		if k > 1 {
			b = append(b, '.')
			b = append(b, digits[1:]...)
		}
		b = append(b, 'e')
		if point > 0 {
			b = append(b, '+')
		} else {
			b = append(b, '-')
		}
		e := point - 1
		if e < 0 {
			e = -e
		}
		b = strconv.AppendInt(b, int64(e), 10)
	}
	return b
}

// jw is a small streaming writer for JSON objects and arrays.
type jw struct {
	b []byte
	// first is true right after an opening brace or bracket.
	first bool
}

func (w *jw) sep() {
	if !w.first {
		w.b = append(w.b, ',')
	}
	w.first = false
}

func (w *jw) open(c byte)  { w.sep(); w.b = append(w.b, c); w.first = true }
func (w *jw) close(c byte) { w.b = append(w.b, c); w.first = false }

// key writes an object key; the value that follows must not call sep again.
func (w *jw) key(k string) {
	w.sep()
	w.b = appendJSString(w.b, k)
	w.b = append(w.b, ':')
	w.first = true
}

func (w *jw) str(s string)   { w.sep(); w.b = appendJSString(w.b, s) }
func (w *jw) num(n float64)  { w.sep(); w.b = appendJSNumber(w.b, n) }
func (w *jw) integer(n int)  { w.sep(); w.b = appendJSNumber(w.b, float64(n)) }
func (w *jw) null()          { w.sep(); w.b = append(w.b, "null"...) }
func (w *jw) boolean(v bool) { w.sep(); w.b = strconv.AppendBool(w.b, v) }

func (w *jw) optStr(s *string) {
	if s == nil {
		w.null()
	} else {
		w.str(*s)
	}
}

func (w *jw) optInt(n *int) {
	if n == nil {
		w.null()
	} else {
		w.integer(*n)
	}
}

func (w *jw) optNum(n *float64) {
	if n == nil {
		w.null()
	} else {
		w.num(*n)
	}
}

func (w *jw) strings(list []string) {
	w.open('[')
	for _, s := range list {
		w.str(s)
	}
	w.close(']')
}

func (w *jw) ints(list []int) {
	w.open('[')
	for _, n := range list {
		w.integer(n)
	}
	w.close(']')
}

// ---------------------------------------------------------------------------------------------
// The public types. Keys in the schema's order, every key present, nil slices as [].

func (a *Artist) write(w *jw) {
	w.open('{')
	w.key("name")
	w.str(a.Name)
	w.key("role")
	w.str(string(a.Role))
	w.key("joiner")
	w.optStr(a.Joiner)
	w.key("source")
	w.str(string(a.Source))
	w.close('}')
}

func writeArtists(w *jw, list []Artist) {
	w.open('[')
	for i := range list {
		list[i].write(w)
	}
	w.close(']')
}

func (v *Version) write(w *jw) {
	w.open('{')
	w.key("type")
	w.str(string(v.Type))
	w.key("raw")
	w.str(v.Raw)
	w.key("artists")
	writeArtists(w, v.Artists)
	w.key("modifiers")
	w.strings(v.Modifiers)
	w.key("descriptor")
	w.optStr(v.Descriptor)
	w.key("year")
	w.optInt(v.Year)
	w.key("unknownArtist")
	w.boolean(v.UnknownArtist)
	w.key("delimiter")
	w.str(string(v.Delimiter))
	w.close('}')
}

func (j *Junk) write(w *jw) {
	w.open('{')
	w.key("raw")
	w.str(j.Raw)
	w.key("kind")
	w.str(string(j.Kind))
	w.close('}')
}

func (f *Flags) write(w *jw) {
	w.open('{')
	w.key("explicit")
	w.boolean(f.Explicit)
	w.key("clean")
	w.boolean(f.Clean)
	w.key("unknownArtist")
	w.boolean(f.UnknownArtist)
	w.key("unknownTitle")
	w.boolean(f.UnknownTitle)
	w.close('}')
}

func (p *Position) write(w *jw) {
	w.open('{')
	w.key("raw")
	w.str(p.Raw)
	w.key("number")
	w.integer(p.Number)
	w.close('}')
}

func (t *Timestamp) write(w *jw) {
	w.open('{')
	w.key("raw")
	w.str(t.Raw)
	w.key("seconds")
	w.integer(t.Seconds)
	w.close('}')
}

func (t *Track) write(w *jw) {
	w.open('{')
	w.key("input")
	w.str(t.Input)
	w.key("mode")
	w.str(string(t.Mode))
	w.key("position")
	if t.Position == nil {
		w.null()
	} else {
		t.Position.write(w)
	}
	w.key("timestamp")
	if t.Timestamp == nil {
		w.null()
	} else {
		t.Timestamp.write(w)
	}
	w.key("artists")
	writeArtists(w, t.Artists)
	w.key("title")
	w.str(t.Title)
	w.key("fullTitle")
	w.str(t.FullTitle)
	w.key("versions")
	w.open('[')
	for i := range t.Versions {
		t.Versions[i].write(w)
	}
	w.close(']')
	w.key("year")
	w.optInt(t.Year)
	w.key("flags")
	t.Flags.write(w)
	w.key("junk")
	w.open('[')
	for i := range t.Junk {
		t.Junk[i].write(w)
	}
	w.close(']')
	w.key("warnings")
	w.open('[')
	for _, x := range t.Warnings {
		w.str(string(x))
	}
	w.close(']')
	w.close('}')
}

func (p *Parts) write(w *jw) {
	w.open('{')
	w.key("lead")
	w.str(p.Lead)
	w.key("time")
	w.str(p.Time)
	w.key("gap")
	w.str(p.Gap)
	w.key("rest")
	w.str(p.Rest)
	w.close('}')
}

func (t *TracklistTime) write(w *jw) {
	w.open('{')
	w.key("raw")
	w.str(t.Raw)
	w.key("seconds")
	w.num(t.Seconds)
	w.close('}')
}

func (e *Entry) write(w *jw) {
	w.open('{')
	w.key("line")
	w.integer(e.Line)
	w.key("kind")
	w.str(string(e.Kind))
	w.key("eol")
	w.str(string(e.Eol))
	w.key("raw")
	w.str(e.Raw)
	w.key("parts")
	e.Parts.write(w)
	w.key("time")
	if e.Time == nil {
		w.null()
	} else {
		e.Time.write(w)
	}
	w.key("track")
	if e.Track == nil {
		w.null()
	} else {
		e.Track.write(w)
	}
	w.key("layered")
	w.boolean(e.Layered)
	w.close('}')
}

func (s *Suggestion) write(w *jw) {
	w.open('{')
	w.key("line")
	w.integer(s.Line)
	w.key("raw")
	w.str(s.Raw)
	w.key("seconds")
	w.num(s.Seconds)
	w.close('}')
}

func (x *TracklistWarning) write(w *jw) {
	w.open('{')
	w.key("code")
	w.str(string(x.Code))
	w.key("line")
	w.integer(x.Line)
	w.key("severity")
	w.str(string(x.Severity))
	w.key("suggestion")
	if x.Suggestion == nil {
		w.null()
	} else {
		x.Suggestion.write(w)
	}
	w.close('}')
}

func (s *Stats) write(w *jw) {
	w.open('{')
	w.key("lines")
	w.integer(s.Lines)
	w.key("tracks")
	w.integer(s.Tracks)
	w.key("markers")
	w.integer(s.Markers)
	w.key("timed")
	w.integer(s.Timed)
	w.key("firstTime")
	w.optNum(s.FirstTime)
	w.key("lastTime")
	w.optNum(s.LastTime)
	w.close('}')
}

func (t *Tracklist) write(w *jw) {
	w.open('{')
	w.key("specVersion")
	w.str(t.SpecVersion)
	w.key("entries")
	w.open('[')
	for i := range t.Entries {
		t.Entries[i].write(w)
	}
	w.close(']')
	w.key("warnings")
	w.open('[')
	for i := range t.Warnings {
		t.Warnings[i].write(w)
	}
	w.close(']')
	w.key("stats")
	t.Stats.write(w)
	w.close('}')
}

func (e *Edit) write(w *jw) {
	w.open('{')
	w.key("op")
	w.str(string(e.Op))
	switch e.Op {
	case EditOpSetTime:
		w.key("line")
		w.integer(e.Line)
		w.key("seconds")
		w.num(e.Seconds)
	case EditOpShift:
		w.key("seconds")
		w.num(e.Seconds)
		if e.Lines != nil {
			w.key("lines")
			w.ints(e.Lines)
		}
		if e.From != nil {
			w.key("from")
			w.integer(*e.From)
		}
	case EditOpSetRest:
		w.key("line")
		w.integer(e.Line)
		w.key("rest")
		w.str(e.Rest)
	case EditOpInsert:
		w.key("after")
		w.integer(e.After)
		w.key("text")
		w.str(e.Text)
	case EditOpDelete:
		w.key("line")
		w.integer(e.Line)
	}
	w.close('}')
}

func (x *EditWarning) write(w *jw) {
	w.open('{')
	w.key("code")
	w.str(string(x.Code))
	w.key("edit")
	w.integer(x.Edit)
	w.key("line")
	w.optInt(x.Line)
	w.close('}')
}

func (r *EditResult) write(w *jw) {
	w.open('{')
	w.key("text")
	w.str(r.Text)
	w.key("changed")
	w.ints(r.Changed)
	w.key("warnings")
	w.open('[')
	for i := range r.Warnings {
		r.Warnings[i].write(w)
	}
	w.close(']')
	w.close('}')
}

func (r *rawEditResult) write(w *jw) {
	w.open('{')
	w.key("text")
	w.str(r.text)
	w.key("changed")
	w.ints(r.changed)
	w.key("warnings")
	w.open('[')
	for _, x := range r.warnings {
		w.open('{')
		w.key("code")
		w.str(string(x.code))
		w.key("edit")
		w.integer(x.edit)
		w.key("line")
		w.optNum(x.line)
		w.close('}')
	}
	w.close(']')
	w.close('}')
}

// writer is implemented by every type with a JSON form.
type writer interface{ write(w *jw) }

// marshal writes v as compact JSON text.
func marshal(v writer) []byte {
	w := jw{first: true}
	v.write(&w)
	return w.b
}

// MarshalJSON writes the spec's JSON form: camelCase keys in the schema's order, every key
// present. The bytes are those of JSON.stringify in the JavaScript package.
func (a Artist) MarshalJSON() ([]byte, error) { return marshal(&a), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (v Version) MarshalJSON() ([]byte, error) { return marshal(&v), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (j Junk) MarshalJSON() ([]byte, error) { return marshal(&j), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (f Flags) MarshalJSON() ([]byte, error) { return marshal(&f), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (p Position) MarshalJSON() ([]byte, error) { return marshal(&p), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (t Timestamp) MarshalJSON() ([]byte, error) { return marshal(&t), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]). [Track.UnmarshalJSON]
// reads it back.
func (t Track) MarshalJSON() ([]byte, error) { return marshal(&t), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (p Parts) MarshalJSON() ([]byte, error) { return marshal(&p), nil }

// MarshalJSON writes the spec's JSON form; a whole number of seconds is an integer.
func (t TracklistTime) MarshalJSON() ([]byte, error) { return marshal(&t), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (e Entry) MarshalJSON() ([]byte, error) { return marshal(&e), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (s Suggestion) MarshalJSON() ([]byte, error) { return marshal(&s), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (x TracklistWarning) MarshalJSON() ([]byte, error) { return marshal(&x), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (s Stats) MarshalJSON() ([]byte, error) { return marshal(&s), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (t Tracklist) MarshalJSON() ([]byte, error) { return marshal(&t), nil }

// MarshalJSON writes the edit as the spec does ({"op":"set-time","line":3,"seconds":90}), with
// only the fields of its operation.
func (e Edit) MarshalJSON() ([]byte, error) { return marshal(&e), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (x EditWarning) MarshalJSON() ([]byte, error) { return marshal(&x), nil }

// MarshalJSON writes the spec's JSON form (see [Artist.MarshalJSON]).
func (r EditResult) MarshalJSON() ([]byte, error) { return marshal(&r), nil }

// ---------------------------------------------------------------------------------------------
// JSON values and the reader

// jsonKind is the type of a jsonValue.
type jsonKind uint8

const (
	jsonNull jsonKind = iota
	jsonBool
	jsonNumber
	jsonString
	jsonArray
	jsonObject
)

// jsonValue is a parsed JSON value. Objects keep their keys in order of first appearance.
type jsonValue struct {
	kind jsonKind
	b    bool
	num  float64
	str  string
	arr  []jsonValue
	obj  []jsonMember
}

// jsonMember is one key of a JSON object.
type jsonMember struct {
	key string
	val jsonValue
}

func jNull() jsonValue             { return jsonValue{} }
func jBool(b bool) jsonValue       { return jsonValue{kind: jsonBool, b: b} }
func jNum(n float64) jsonValue     { return jsonValue{kind: jsonNumber, num: n} }
func jStr(s string) jsonValue      { return jsonValue{kind: jsonString, str: s} }
func jArr(a []jsonValue) jsonValue { return jsonValue{kind: jsonArray, arr: a} }

// get is the value of key in an object; false when v is not an object or has no such key.
func (v jsonValue) get(key string) (jsonValue, bool) {
	if v.kind != jsonObject {
		return jsonValue{}, false
	}
	for _, m := range v.obj {
		if m.key == key {
			return m.val, true
		}
	}
	return jsonValue{}, false
}

// getString is the string at key, or "" and false.
func (v jsonValue) getString(key string) (string, bool) {
	x, ok := v.get(key)
	if !ok || x.kind != jsonString {
		return "", false
	}
	return x.str, true
}

func (v jsonValue) write(w *jw) {
	switch v.kind {
	case jsonNull:
		w.null()
	case jsonBool:
		w.boolean(v.b)
	case jsonNumber:
		w.num(v.num)
	case jsonString:
		w.str(v.str)
	case jsonArray:
		w.open('[')
		for _, x := range v.arr {
			x.write(w)
		}
		w.close(']')
	case jsonObject:
		w.open('{')
		for _, m := range v.obj {
			w.key(m.key)
			m.val.write(w)
		}
		w.close('}')
	}
}

// jsonOf is the JSON value of anything with a JSON form (through its text).
func jsonOf(v writer) jsonValue {
	out, _ := parseJSON(marshal(v))
	return out
}

// errJSON is the one error of the reader: the text is not JSON it can read.
var errJSON = errors.New("trackparse: invalid JSON")

// maxJSONDepth bounds nesting, so a hostile input cannot exhaust the stack.
const maxJSONDepth = 200

// parseJSON reads one JSON value. It is lenient where the JS reference has to be matched: a
// number is anything strconv reads from the characters 0-9 + - . e E (so 1e999 is infinity),
// an unknown escape stands for its character, and a repeated key keeps its first place and
// takes the last value (JS). Unpaired surrogate escapes become U+FFFD, since a Go string holds
// no lone surrogates. Bytes that are not valid UTF-8 are first replaced (see sanitize).
func parseJSON(text []byte) (jsonValue, error) {
	r := jsonReader{s: sanitize(string(text))}
	r.ws()
	v, ok := r.value()
	if !ok {
		return jsonValue{}, errJSON
	}
	r.ws()
	if r.at != len(r.s) {
		return jsonValue{}, errJSON
	}
	return v, nil
}

type jsonReader struct {
	s     string
	at    int
	depth int
}

// peek is the byte at the cursor, or 0 at the end.
func (r *jsonReader) peek() byte {
	if r.at < len(r.s) {
		return r.s[r.at]
	}
	return 0
}

func (r *jsonReader) ws() {
	for r.at < len(r.s) {
		switch r.s[r.at] {
		case ' ', '\t', '\n', '\r':
			r.at++
		default:
			return
		}
	}
}

func (r *jsonReader) eat(word string) bool {
	if len(r.s)-r.at >= len(word) && r.s[r.at:r.at+len(word)] == word {
		r.at += len(word)
		return true
	}
	return false
}

func (r *jsonReader) value() (jsonValue, bool) {
	if r.depth > maxJSONDepth || r.at >= len(r.s) {
		return jsonValue{}, false
	}
	switch r.peek() {
	case 'n':
		return jNull(), r.eat("null")
	case 't':
		return jBool(true), r.eat("true")
	case 'f':
		return jBool(false), r.eat("false")
	case '"':
		s, ok := r.string()
		return jStr(s), ok
	case '[':
		r.at++
		r.depth++
		items := []jsonValue{}
		r.ws()
		if r.peek() == ']' {
			r.at++
		} else {
			for {
				r.ws()
				v, ok := r.value()
				if !ok {
					return jsonValue{}, false
				}
				items = append(items, v)
				r.ws()
				switch r.peek() {
				case ',':
					r.at++
					continue
				case ']':
					r.at++
				default:
					return jsonValue{}, false
				}
				break
			}
		}
		r.depth--
		return jArr(items), true
	case '{':
		r.at++
		r.depth++
		members := []jsonMember{}
		r.ws()
		if r.peek() == '}' {
			r.at++
		} else {
			for {
				r.ws()
				key, ok := r.string()
				if !ok {
					return jsonValue{}, false
				}
				r.ws()
				if r.peek() != ':' {
					return jsonValue{}, false
				}
				r.at++
				r.ws()
				v, ok := r.value()
				if !ok {
					return jsonValue{}, false
				}
				found := false
				for i := range members {
					if members[i].key == key {
						members[i].val = v
						found = true
						break
					}
				}
				if !found {
					members = append(members, jsonMember{key, v})
				}
				r.ws()
				switch r.peek() {
				case ',':
					r.at++
					continue
				case '}':
					r.at++
				default:
					return jsonValue{}, false
				}
				break
			}
		}
		r.depth--
		return jsonValue{kind: jsonObject, obj: members}, true
	}
	return r.number()
}

func (r *jsonReader) number() (jsonValue, bool) {
	start := r.at
	for r.at < len(r.s) {
		c := r.s[r.at]
		if (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' {
			r.at++
			continue
		}
		break
	}
	if r.at == start {
		return jsonValue{}, false
	}
	f, err := strconv.ParseFloat(r.s[start:r.at], 64)
	if err != nil {
		var ne *strconv.NumError
		if !errors.As(err, &ne) || ne.Err != strconv.ErrRange {
			return jsonValue{}, false
		}
		// Out of range: ParseFloat already gives the infinity (or zero), as JS Number() does.
	}
	return jNum(f), true
}

// hex4 reads four hex digits.
func (r *jsonReader) hex4() (rune, bool) {
	if len(r.s)-r.at < 4 {
		return 0, false
	}
	var n rune
	for i := 0; i < 4; i++ {
		c := r.s[r.at+i]
		var d byte
		switch {
		case c >= '0' && c <= '9':
			d = c - '0'
		case c >= 'a' && c <= 'f':
			d = c - 'a' + 10
		case c >= 'A' && c <= 'F':
			d = c - 'A' + 10
		default:
			return 0, false
		}
		n = n<<4 | rune(d)
	}
	r.at += 4
	return n, true
}

func (r *jsonReader) string() (string, bool) {
	if r.peek() != '"' {
		return "", false
	}
	r.at++
	var out []byte
	for {
		if r.at >= len(r.s) {
			return "", false
		}
		c := r.s[r.at]
		r.at++
		switch c {
		case '"':
			return string(out), true
		case '\\':
			if r.at >= len(r.s) {
				return "", false
			}
			e := r.s[r.at]
			r.at++
			switch e {
			case 'n':
				out = append(out, '\n')
			case 'r':
				out = append(out, '\r')
			case 't':
				out = append(out, '\t')
			case 'b':
				out = append(out, '\b')
			case 'f':
				out = append(out, '\f')
			case 'u':
				hi, ok := r.hex4()
				if !ok {
					return "", false
				}
				cp := hi
				if hi >= 0xd800 && hi < 0xdc00 && r.eat("\\u") {
					lo, ok := r.hex4()
					if !ok {
						return "", false
					}
					if lo >= 0xdc00 && lo < 0xe000 {
						cp = 0x10000 + (hi-0xd800)<<10 + (lo - 0xdc00)
					} else {
						// Two units that do not pair: both become U+FFFD (lo unless it is a
						// scalar value).
						out = utf8.AppendRune(out, utf8.RuneError)
						cp = lo
					}
				}
				// AppendRune writes U+FFFD for a lone surrogate.
				out = utf8.AppendRune(out, cp)
			default:
				// \" \\ \/ and any other escaped character stand for themselves. The escaped
				// character may be the first byte of a multi-byte code point: copy it whole.
				_, size := utf8.DecodeRuneInString(r.s[r.at-1:])
				out = append(out, r.s[r.at-1:r.at-1+size]...)
				r.at += size - 1
			}
		default:
			out = append(out, c)
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Track.UnmarshalJSON

// decoder reads typed fields out of a jsonValue and keeps the first error.
type decoder struct{ err error }

func (d *decoder) fail(path, want string) {
	if d.err == nil {
		d.err = errors.New("trackparse: " + path + ": expected " + want)
	}
}

func (d *decoder) str(v jsonValue, path string) string {
	if v.kind != jsonString {
		d.fail(path, "a string")
		return ""
	}
	return v.str
}

func (d *decoder) optStr(v jsonValue, path string) *string {
	if v.kind == jsonNull {
		return nil
	}
	s := d.str(v, path)
	return &s
}

func (d *decoder) boolean(v jsonValue, path string) bool {
	if v.kind != jsonBool {
		d.fail(path, "a boolean")
		return false
	}
	return v.b
}

func (d *decoder) integer(v jsonValue, path string) int {
	if v.kind != jsonNumber || v.num != math.Trunc(v.num) || math.IsInf(v.num, 0) {
		d.fail(path, "an integer")
		return 0
	}
	return saturatingInt(v.num)
}

func (d *decoder) optInt(v jsonValue, path string) *int {
	if v.kind == jsonNull {
		return nil
	}
	n := d.integer(v, path)
	return &n
}

// each calls f on every element of an array (a missing key is an empty array).
func (d *decoder) each(v jsonValue, path string, f func(x jsonValue, path string)) {
	if v.kind == jsonNull {
		return
	}
	if v.kind != jsonArray {
		d.fail(path, "an array")
		return
	}
	for i, x := range v.arr {
		f(x, path+"["+strconv.Itoa(i)+"]")
	}
}

// fields calls f on every key of an object.
func (d *decoder) fields(v jsonValue, path string, f func(key string, x jsonValue, path string)) {
	if v.kind != jsonObject {
		d.fail(path, "an object")
		return
	}
	for _, m := range v.obj {
		f(m.key, m.val, path+"."+m.key)
	}
}

func (d *decoder) artist(v jsonValue, path string) Artist {
	var a Artist
	d.fields(v, path, func(key string, x jsonValue, p string) {
		switch key {
		case "name":
			a.Name = d.str(x, p)
		case "role":
			a.Role = Role(d.str(x, p))
		case "joiner":
			a.Joiner = d.optStr(x, p)
		case "source":
			a.Source = Source(d.str(x, p))
		}
	})
	return a
}

func (d *decoder) artists(v jsonValue, path string) []Artist {
	out := []Artist{}
	d.each(v, path, func(x jsonValue, p string) { out = append(out, d.artist(x, p)) })
	return out
}

func (d *decoder) version(v jsonValue, path string) Version {
	out := Version{Artists: []Artist{}, Modifiers: []string{}}
	d.fields(v, path, func(key string, x jsonValue, p string) {
		switch key {
		case "type":
			out.Type = VersionType(d.str(x, p))
		case "raw":
			out.Raw = d.str(x, p)
		case "artists":
			out.Artists = d.artists(x, p)
		case "modifiers":
			d.each(x, p, func(m jsonValue, mp string) { out.Modifiers = append(out.Modifiers, d.str(m, mp)) })
		case "descriptor":
			out.Descriptor = d.optStr(x, p)
		case "year":
			out.Year = d.optInt(x, p)
		case "unknownArtist":
			out.UnknownArtist = d.boolean(x, p)
		case "delimiter":
			out.Delimiter = Delimiter(d.str(x, p))
		}
	})
	return out
}

// track reads a Track; missing keys keep their zero value (slices empty).
func (d *decoder) track(v jsonValue, path string) Track {
	t := Track{Artists: []Artist{}, Versions: []Version{}, Junk: []Junk{}, Warnings: []Warning{}}
	d.fields(v, path, func(key string, x jsonValue, p string) {
		switch key {
		case "input":
			t.Input = d.str(x, p)
		case "mode":
			t.Mode = Mode(d.str(x, p))
		case "position":
			if x.kind != jsonNull {
				pos := Position{}
				d.fields(x, p, func(k string, y jsonValue, q string) {
					switch k {
					case "raw":
						pos.Raw = d.str(y, q)
					case "number":
						pos.Number = d.integer(y, q)
					}
				})
				t.Position = &pos
			}
		case "timestamp":
			if x.kind != jsonNull {
				ts := Timestamp{}
				d.fields(x, p, func(k string, y jsonValue, q string) {
					switch k {
					case "raw":
						ts.Raw = d.str(y, q)
					case "seconds":
						ts.Seconds = d.integer(y, q)
					}
				})
				t.Timestamp = &ts
			}
		case "artists":
			t.Artists = d.artists(x, p)
		case "title":
			t.Title = d.str(x, p)
		case "fullTitle":
			t.FullTitle = d.str(x, p)
		case "versions":
			d.each(x, p, func(y jsonValue, q string) { t.Versions = append(t.Versions, d.version(y, q)) })
		case "year":
			t.Year = d.optInt(x, p)
		case "flags":
			d.fields(x, p, func(k string, y jsonValue, q string) {
				switch k {
				case "explicit":
					t.Flags.Explicit = d.boolean(y, q)
				case "clean":
					t.Flags.Clean = d.boolean(y, q)
				case "unknownArtist":
					t.Flags.UnknownArtist = d.boolean(y, q)
				case "unknownTitle":
					t.Flags.UnknownTitle = d.boolean(y, q)
				}
			})
		case "junk":
			d.each(x, p, func(y jsonValue, q string) {
				j := Junk{}
				d.fields(y, q, func(k string, z jsonValue, r string) {
					switch k {
					case "raw":
						j.Raw = d.str(z, r)
					case "kind":
						j.Kind = JunkKind(d.str(z, r))
					}
				})
				t.Junk = append(t.Junk, j)
			})
		case "warnings":
			d.each(x, p, func(y jsonValue, q string) { t.Warnings = append(t.Warnings, Warning(d.str(y, q))) })
		}
	})
	return t
}

// UnmarshalJSON reads the spec's JSON form of a track, as [Track.MarshalJSON] writes it.
// Unknown keys are ignored and missing keys keep their zero value; a key of the wrong type is
// an error. Enum values are taken as written.
func (t *Track) UnmarshalJSON(data []byte) error {
	v, err := parseJSON(data)
	if err != nil {
		return err
	}
	var d decoder
	out := d.track(v, "$")
	if d.err != nil {
		return d.err
	}
	*t = out
	return nil
}
