package trackparse

import "math"

// R12 options: the public option structs, the edit constructors, and the resolved forms the
// pipeline reads.
//
// Two doors lead to a resolved form: the typed structs below, and loose JSON (internal/wire),
// which is resolved with exactly the fallbacks of the JS reference (a bad value means the
// default, R0.8). The typed enums are strings, so the typed door applies the same fallbacks to
// a string the JSON door would get: an unknown name behaves as it does in JavaScript. Only the
// empty string differs: it is the zero value, which means the default. Resolution never fails.

// SplitAnd is when the word "and" splits a credit list (R7.3). "&" and the other joiners always
// split. The zero value means [SplitAndAuto].
type SplitAnd string

// The SplitAnd values. In [ParseOptions] any other string behaves as [SplitAndAlways], as in
// the JavaScript package; in [TracklistOptions] it behaves as [SplitAndAuto].
const (
	SplitAndAuto   SplitAnd = "auto"
	SplitAndAlways SplitAnd = "always"
	SplitAndNever  SplitAnd = "never"
)

// FeatPlacement is where [FormatWith] writes featured artists (R11.2). The zero value means
// [FeatSource].
type FeatPlacement string

// The FeatPlacement values. Any other string places featured artists nowhere, as in the
// JavaScript package.
const (
	FeatSource FeatPlacement = "source"
	FeatArtist FeatPlacement = "artist"
	FeatTitle  FeatPlacement = "title"
	FeatOmit   FeatPlacement = "omit"
)

// JoinerStyle is whether [FormatWith] writes joiners as written or in their canonical form
// (R11.5). The zero value means [JoinersOriginal].
type JoinerStyle string

// The JoinerStyle values. Any other string behaves as [JoinersOriginal].
const (
	JoinersOriginal  JoinerStyle = "original"
	JoinersCanonical JoinerStyle = "canonical"
)

// VersionsOption is whether [FormatWith] writes the versions (R11.3). The zero value means
// [VersionsAll].
type VersionsOption string

// The VersionsOption values. Any other string behaves as [VersionsNone], as in the JavaScript
// package.
const (
	VersionsAll  VersionsOption = "all"
	VersionsNone VersionsOption = "none"
)

// VersionHead is an extra version head word and the version type it means
// (`{Key: "rerub", Type: VersionTypeRemix}`).
type VersionHead struct {
	// Key is the word or phrase, matched with ASCII lowercasing.
	Key string
	// Type is the version type it gives. Any string is kept as given in the output.
	Type VersionType
}

// JunkPhrase is an extra junk phrase and its kind (`{Phrase: "out now", Kind: JunkKindPromo}`).
type JunkPhrase struct {
	// Phrase is the phrase, matched with ASCII lowercasing.
	Phrase string
	// Kind is the junk kind it gives. Any string is kept as given in the output.
	Kind JunkKind
}

// Keywords is extra vocabulary, added to the built-in word lists (R12). The two maps of the
// JavaScript options are ordered slices here: a repeated key keeps its first place and takes
// the last value, and integer-like keys come first in ascending order, exactly as JavaScript
// reads an object.
type Keywords struct {
	// VersionHeads are extra version heads.
	VersionHeads []VersionHead
	// Descriptors are extra descriptor words (Extended, Deluxe).
	Descriptors []string
	// Genres are extra genre names.
	Genres []string
	// Junk are extra junk phrases.
	Junk []JunkPhrase
	// FeatMarkers are extra markers that introduce featured artists.
	FeatMarkers []string
}

// isEmpty reports whether k adds no word, so the default tables can be shared.
func (k *Keywords) isEmpty() bool {
	return len(k.VersionHeads) == 0 && len(k.Descriptors) == 0 && len(k.Genres) == 0 &&
		len(k.Junk) == 0 && len(k.FeatMarkers) == 0
}

// ParseOptions are the options of [ParseWith], [ParseArtistsWith] and [NewParser] (R12). The
// zero value is what [Parse] uses.
type ParseOptions struct {
	// Mode is the mode; the zero value [ModeAuto] picks one from the string.
	Mode Mode
	// Uploader is the channel name, used in youtube mode as the artist when the title has no
	// separator (R6.8). Empty means none.
	Uploader string
	// KnownArtists are names that must never be split (R7.3a): Chase & Status, Above & Beyond.
	KnownArtists []string
	// SplitAnd is when "and" splits a credit list; the zero value means [SplitAndAuto].
	SplitAnd SplitAnd
	// Keywords is extra vocabulary.
	Keywords Keywords
}

// FormatOptions are the options of [FormatWith] (R11). The zero value is what [Format] uses.
type FormatOptions struct {
	// Feat is where featured artists go; the zero value means [FeatSource] (where they were
	// found).
	Feat FeatPlacement
	// FeatMarker is the marker written in front of featured lists instead of the one in the
	// input. Empty means the input's marker.
	FeatMarker string
	// Joiners is the joiner style; the zero value means [JoinersOriginal].
	Joiners JoinerStyle
	// Versions is whether versions are written; the zero value means [VersionsAll].
	Versions VersionsOption
	// OmitProducers leaves out the prod. credits (the JavaScript option producers: false).
	OmitProducers bool
	// Position writes the position in front.
	Position bool
}

// TracklistOptions are the options of [ParseTracklistWith] and [ApplyEditsWith] (R13.10). The
// zero value is what [ParseTracklist] uses.
type TracklistOptions struct {
	// Mode is the per-line parse mode; the zero value means [ModeTracklist]. [ModeAuto] cannot
	// be asked for with the zero value: use the string "auto" (Mode("auto")).
	Mode Mode
	// KnownArtists are names that must never be split (R7.3a).
	KnownArtists []string
	// SplitAnd is when "and" splits a credit list; the zero value means [SplitAndAuto].
	SplitAnd SplitAnd
	// Keywords is extra vocabulary for the per-line parse.
	Keywords Keywords
	// AudioSeconds is the length of the audio in seconds; it enables time-after-audio-end.
	// Zero, negative and non-finite values mean unknown. (The JavaScript package takes 0 as an
	// audio of length 0; the zero value cannot say that here.)
	AudioSeconds float64
	// MarkerWords are extra marker words (R13.3): a line that is only such a word is a marker.
	MarkerWords []string
}

// SetTime is the edit that rewrites the time of line, keeping its written style (R13.9.2).
func SetTime(line int, seconds float64) Edit {
	return Edit{Op: EditOpSetTime, Line: line, Seconds: seconds}
}

// Shift is the edit that adds seconds to every timed line of the text.
func Shift(seconds float64) Edit {
	return Edit{Op: EditOpShift, Seconds: seconds}
}

// ShiftFrom is the edit that adds seconds to every timed line from line from on.
func ShiftFrom(seconds float64, from int) Edit {
	return Edit{Op: EditOpShift, Seconds: seconds, From: &from}
}

// ShiftLines is the edit that adds seconds to the given lines only.
func ShiftLines(seconds float64, lines ...int) Edit {
	return Edit{Op: EditOpShift, Seconds: seconds, Lines: append([]int{}, lines...)}
}

// SortByTime is the edit that sorts the timed lines stably, each with the untimed lines after
// it (R13.9.4).
func SortByTime() Edit {
	return Edit{Op: EditOpSortByTime}
}

// SetRest is the edit that replaces everything after the time and gap of line.
func SetRest(line int, rest string) Edit {
	return Edit{Op: EditOpSetRest, Line: line, Rest: rest}
}

// Insert is the edit that inserts text as a new line after line after (0: at the start).
func Insert(after int, text string) Edit {
	return Edit{Op: EditOpInsert, After: after, Text: text}
}

// Delete is the edit that removes line.
func Delete(line int) Edit {
	return Edit{Op: EditOpDelete, Line: line}
}

// ---------------------------------------------------------------------------------------------
// Resolved forms

// requestKind is how options.mode reached the pipeline.
type requestKind uint8

const (
	// requestAuto: missing, null or "auto".
	requestAuto requestKind = iota
	// requestMode: one of the four mode names.
	requestMode
	// requestOther: any other value. JS then skips the R2.1 youtube detection but still
	// detects filenames.
	requestOther
)

// requestedMode is options.mode as the pipeline sees it; mode is set for requestMode only.
type requestedMode struct {
	kind requestKind
	mode Mode
}

// splitAndMode is options.splitAnd as the pipeline sees it.
type splitAndMode uint8

const (
	splitAuto splitAndMode = iota
	splitAlways
	splitNever
)

// kv is one key/value pair of an options object, in JS Object.entries order.
type kv struct {
	key   string
	value string
}

// keywordLists is options.keywords with the maps in JS Object.entries order. Types and kinds
// stay strings: JS passes any string value through to the output.
type keywordLists struct {
	versionHeads []kv
	descriptors  []string
	genres       []string
	junk         []kv
	featMarkers  []string
}

// resolvedOptions are parse options after the R0.8 fallbacks.
type resolvedOptions struct {
	mode requestedMode
	// uploader is the R6.8 channel name; empty means none (JS treats "" as none too).
	uploader     string
	knownArtists []string
	splitAnd     splitAndMode
	// keywords is nil for the default tables.
	keywords *keywordLists
}

// featMode is options.feat as R11 reads it: an unknown value places featured artists nowhere.
type featMode uint8

const (
	featSource featMode = iota
	featArtist
	featTitle
	featOmit
)

// resolvedFormat are format options after the fallbacks.
type resolvedFormat struct {
	feat featMode
	// featMarker is nil when the input's marker is kept.
	featMarker *string
	canonical  bool
	versions   bool
	producers  bool
	position   bool
}

// resolvedTracklist are tracklist options after the R13.10 fallbacks.
type resolvedTracklist struct {
	// parse are the options of the per-line parse.
	parse resolvedOptions
	// audioSeconds is nil when unknown.
	audioSeconds *float64
	markerWords  []string
}

// modeByName is a Mode for one of the four names of a parsed mode.
func modeByName(name string) (Mode, bool) {
	switch Mode(name) {
	case ModeClean, ModeYouTube, ModeFilename, ModeTracklist:
		return Mode(name), true
	}
	return "", false
}

// requestFor is the R2 request for a mode name given as a string.
func requestFor(name string) requestedMode {
	if name == "auto" {
		return requestedMode{kind: requestAuto}
	}
	if m, ok := modeByName(name); ok {
		return requestedMode{kind: requestMode, mode: m}
	}
	return requestedMode{kind: requestOther}
}

// parseSplitAnd is options.splitAnd of a parse: anything but auto and never (or missing)
// behaves as always in JS.
func parseSplitAnd(name string) splitAndMode {
	switch name {
	case "auto":
		return splitAuto
	case "never":
		return splitNever
	}
	return splitAlways
}

// tracklistSplitAnd is options.splitAnd of a tracklist: only always and never count.
func tracklistSplitAnd(name string) splitAndMode {
	switch name {
	case "always":
		return splitAlways
	case "never":
		return splitNever
	}
	return splitAuto
}

// tracklistRequest is options.mode of a tracklist: a mode name or auto; anything else means
// tracklist.
func tracklistRequest(name string) requestedMode {
	if name == "auto" {
		return requestedMode{kind: requestAuto}
	}
	if m, ok := modeByName(name); ok {
		return requestedMode{kind: requestMode, mode: m}
	}
	return requestedMode{kind: requestMode, mode: ModeTracklist}
}

// arrayIndex reports whether key is a canonical array index ("0", "17"; not "01"), which JS
// lists first among an object's keys, in ascending order.
func arrayIndex(key string) (uint64, bool) {
	if key == "" || len(key) > 10 {
		return 0, false
	}
	if len(key) > 1 && key[0] == '0' {
		return 0, false
	}
	var n uint64
	for i := 0; i < len(key); i++ {
		c := key[i]
		if c < '0' || c > '9' {
			return 0, false
		}
		n = n*10 + uint64(c-'0')
	}
	return n, n <= 0xfffffffe
}

// jsEntries is the Object.entries order of an object built from pairs: a repeated key keeps
// its first place and takes the last value; integer-like keys come first in ascending order,
// then the others in insertion order.
func jsEntries(pairs []kv) []kv {
	unique := make([]kv, 0, len(pairs))
	index := make(map[string]int, len(pairs))
	for _, p := range pairs {
		if i, ok := index[p.key]; ok {
			unique[i].value = p.value
			continue
		}
		index[p.key] = len(unique)
		unique = append(unique, p)
	}
	type indexed struct {
		n uint64
		p kv
	}
	var ints []indexed
	rest := make([]kv, 0, len(unique))
	for _, p := range unique {
		if n, ok := arrayIndex(p.key); ok {
			ints = append(ints, indexed{n, p})
		} else {
			rest = append(rest, p)
		}
	}
	// Keys are unique, so the order of ints is total; insertion sort keeps it simple and stable.
	for i := 1; i < len(ints); i++ {
		for j := i; j > 0 && ints[j].n < ints[j-1].n; j-- {
			ints[j], ints[j-1] = ints[j-1], ints[j]
		}
	}
	out := make([]kv, 0, len(unique))
	for _, x := range ints {
		out = append(out, x.p)
	}
	return append(out, rest...)
}

// sanitizeAll is sanitize over a list of strings (a fresh slice, never the caller's).
func sanitizeAll(list []string) []string {
	out := make([]string, len(list))
	for i, s := range list {
		out[i] = sanitize(s)
	}
	return out
}

// resolveKeywords is the typed keywords; nil when they add nothing.
func resolveKeywords(k *Keywords) *keywordLists {
	if k.isEmpty() {
		return nil
	}
	heads := make([]kv, 0, len(k.VersionHeads))
	for _, h := range k.VersionHeads {
		heads = append(heads, kv{sanitize(h.Key), sanitize(string(h.Type))})
	}
	junk := make([]kv, 0, len(k.Junk))
	for _, j := range k.Junk {
		junk = append(junk, kv{sanitize(j.Phrase), sanitize(string(j.Kind))})
	}
	return &keywordLists{
		versionHeads: jsEntries(heads),
		descriptors:  sanitizeAll(k.Descriptors),
		genres:       sanitizeAll(k.Genres),
		junk:         jsEntries(junk),
		featMarkers:  sanitizeAll(k.FeatMarkers),
	}
}

// resolveParse is the typed door to resolvedOptions.
func resolveParse(o *ParseOptions) resolvedOptions {
	r := resolvedOptions{
		uploader:     sanitize(o.Uploader),
		knownArtists: sanitizeAll(o.KnownArtists),
		keywords:     resolveKeywords(&o.Keywords),
	}
	if o.Mode != ModeAuto {
		r.mode = requestFor(string(o.Mode))
	}
	if o.SplitAnd != "" {
		r.splitAnd = parseSplitAnd(string(o.SplitAnd))
	}
	return r
}

// resolveFormat is the typed door to resolvedFormat.
func resolveFormat(o *FormatOptions) resolvedFormat {
	r := resolvedFormat{
		canonical: o.Joiners == JoinersCanonical,
		versions:  o.Versions == "" || o.Versions == VersionsAll,
		producers: !o.OmitProducers,
		position:  o.Position,
	}
	switch o.Feat {
	case "", FeatSource:
		r.feat = featSource
	case FeatArtist:
		r.feat = featArtist
	case FeatTitle:
		r.feat = featTitle
	default:
		r.feat = featOmit
	}
	if o.FeatMarker != "" {
		m := sanitize(o.FeatMarker)
		r.featMarker = &m
	}
	return r
}

// audio is the R13.10 audio length: nil unless finite and not negative.
func audio(seconds float64, given bool) *float64 {
	if !given || math.IsNaN(seconds) || math.IsInf(seconds, 0) || seconds < 0 {
		return nil
	}
	return &seconds
}

// resolveTracklist is the typed door to resolvedTracklist.
func resolveTracklist(o *TracklistOptions) resolvedTracklist {
	r := resolvedTracklist{
		parse: resolvedOptions{
			mode:         requestedMode{kind: requestMode, mode: ModeTracklist},
			knownArtists: sanitizeAll(o.KnownArtists),
			keywords:     resolveKeywords(&o.Keywords),
		},
		audioSeconds: audio(o.AudioSeconds, o.AudioSeconds != 0),
		markerWords:  sanitizeAll(o.MarkerWords),
	}
	if o.Mode != "" {
		r.parse.mode = tracklistRequest(string(o.Mode))
	}
	r.parse.splitAnd = tracklistSplitAnd(string(o.SplitAnd))
	return r
}

// ---------------------------------------------------------------------------------------------
// The JSON door (internal/wire): loose values with the JS fallbacks.

// stringList is an option list as JS reads it at run time (R0.8): only an array's strings
// count.
func stringList(v jsonValue, ok bool) []string {
	if !ok || v.kind != jsonArray {
		return nil
	}
	var out []string
	for _, item := range v.arr {
		if item.kind == jsonString {
			out = append(out, item.str)
		}
	}
	return out
}

// stringEntries is an option map as JS reads it at run time (R0.8): only a plain object's
// string values count, in Object.entries order.
func stringEntries(v jsonValue, ok bool) []kv {
	if !ok || v.kind != jsonObject {
		return nil
	}
	pairs := make([]kv, 0, len(v.obj))
	for _, m := range v.obj {
		if m.val.kind == jsonString {
			pairs = append(pairs, kv{m.key, m.val.str})
		}
	}
	// The parser already merged repeated keys; jsEntries only reorders integer-like keys.
	return jsEntries(pairs)
}

// keywordsJSON is options.keywords from loose JSON (a plain object only).
func keywordsJSON(v jsonValue) *keywordLists {
	return &keywordLists{
		versionHeads: stringEntries(v.get("versionHeads")),
		descriptors:  stringList(v.get("descriptors")),
		genres:       stringList(v.get("genres")),
		junk:         stringEntries(v.get("junk")),
		featMarkers:  stringList(v.get("featMarkers")),
	}
}

// resolveParseJSON is the JSON door to resolvedOptions, with the JS fallbacks.
func resolveParseJSON(o jsonValue) resolvedOptions {
	var r resolvedOptions
	if o.kind != jsonObject {
		return r // safeOptions: not an object
	}
	// `options.mode ?? "auto"`
	if m, ok := o.get("mode"); ok && m.kind != jsonNull {
		if m.kind == jsonString {
			r.mode = requestFor(m.str)
		} else {
			r.mode = requestedMode{kind: requestOther}
		}
	}
	// `options.splitAnd ?? "auto"`
	if s, ok := o.get("splitAnd"); ok && s.kind != jsonNull {
		if s.kind == jsonString {
			r.splitAnd = parseSplitAnd(s.str)
		} else {
			r.splitAnd = splitAlways
		}
	}
	// `options.keywords ? ... : baseTables`, then isRecord: only a plain object adds words.
	if k, ok := o.get("keywords"); ok && k.kind == jsonObject {
		r.keywords = keywordsJSON(k)
	}
	if u, ok := o.get("uploader"); ok && u.kind == jsonString {
		r.uploader = u.str
	}
	r.knownArtists = stringList(o.get("knownArtists"))
	return r
}

// truthy is JS truthiness of a JSON value.
func truthy(v jsonValue) bool {
	switch v.kind {
	case jsonNull:
		return false
	case jsonBool:
		return v.b
	case jsonNumber:
		return v.num != 0 && !math.IsNaN(v.num)
	case jsonString:
		return v.str != ""
	}
	return true
}

// jsString is JS String(value), what a template literal does with a non-string featMarker.
func jsString(v jsonValue) string {
	switch v.kind {
	case jsonNull:
		return "null"
	case jsonBool:
		if v.b {
			return "true"
		}
		return "false"
	case jsonNumber:
		switch {
		case math.IsNaN(v.num):
			return "NaN"
		case math.IsInf(v.num, 1):
			return "Infinity"
		case math.IsInf(v.num, -1):
			return "-Infinity"
		}
		return string(appendJSNumber(nil, v.num))
	case jsonString:
		return v.str
	case jsonArray:
		// Array.prototype.join: null elements are empty.
		out := ""
		for i, item := range v.arr {
			if i > 0 {
				out += ","
			}
			if item.kind != jsonNull {
				out += jsString(item)
			}
		}
		return out
	}
	return "[object Object]"
}

// resolveFormatJSON is the JSON door to resolvedFormat (`??` for missing and null).
func resolveFormatJSON(o jsonValue) resolvedFormat {
	get := func(key string) (jsonValue, bool) {
		v, ok := o.get(key)
		if !ok || v.kind == jsonNull {
			return jsonValue{}, false
		}
		return v, true
	}
	r := resolvedFormat{versions: true, producers: true}
	if f, ok := get("feat"); ok {
		switch {
		case f.kind == jsonString && f.str == "source":
			r.feat = featSource
		case f.kind == jsonString && f.str == "artist":
			r.feat = featArtist
		case f.kind == jsonString && f.str == "title":
			r.feat = featTitle
		default:
			r.feat = featOmit
		}
	}
	// `o.featMarker !== undefined`: an explicit null is rendered as "null".
	if m, ok := o.get("featMarker"); ok {
		s := jsString(m)
		r.featMarker = &s
	}
	if j, ok := get("joiners"); ok {
		r.canonical = j.kind == jsonString && j.str == "canonical"
	}
	if v, ok := get("versions"); ok {
		r.versions = v.kind == jsonString && v.str == "all"
	}
	if p, ok := get("producers"); ok {
		r.producers = truthy(p)
	}
	if p, ok := get("position"); ok {
		r.position = truthy(p)
	}
	return r
}

// jsNumber is a JS number view of a JSON value: ok only for a number (a bool is not one). The
// value may be NaN or infinite; callers apply their own checks.
func jsNumber(v jsonValue, present bool) (float64, bool) {
	if !present || v.kind != jsonNumber {
		return 0, false
	}
	return v.num, true
}

// resolveTracklistJSON is the JSON door to resolvedTracklist. mode, splitAnd, knownArtists and
// keywords are passed on only when well-formed; uploader is not a tracklist option.
func resolveTracklistJSON(o jsonValue) resolvedTracklist {
	parse := resolveParseJSON(o)
	parse.uploader = ""
	parse.mode = requestedMode{kind: requestMode, mode: ModeTracklist}
	if m, ok := o.get("mode"); ok && m.kind == jsonString {
		parse.mode = tracklistRequest(m.str)
	}
	parse.splitAnd = splitAuto
	if s, ok := o.get("splitAnd"); ok && s.kind == jsonString {
		parse.splitAnd = tracklistSplitAnd(s.str)
	}
	n, ok := jsNumber(o.get("audioSeconds"))
	return resolvedTracklist{
		parse:        parse,
		audioSeconds: audio(n, ok),
		markerWords:  stringList(o.get("markerWords")),
	}
}
