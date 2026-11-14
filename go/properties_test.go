package trackparse

// SPEC.md "Invariants" for single tracks and R13.11 for tracklists, checked on generated
// inputs. The generator is a small deterministic PRNG (mulberry32, as in the Rust suite) over
// the fragments of the JS and Rust property tests plus random code points, so a failure is
// reproducible from its seed and case number.
//
// The checks are functions shared with the fuzz targets in fuzz_test.go: these tests run them
// a few thousand times with a fixed seed on every `go test`, the fuzz targets run them on what
// the fuzzer finds.

import (
	"fmt"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"testing"
)

// ---------------------------------------------------------------------------------------------
// Generator

// rng is mulberry32.
type rng struct{ s uint32 }

func newRNG(seed uint64) *rng { return &rng{s: uint32(seed) ^ uint32(seed>>32)} }

func (r *rng) next() uint32 {
	r.s += 0x6d2b79f5
	t := r.s
	t = (t ^ (t >> 15)) * (t | 1)
	t ^= t + (t^(t>>7))*(t|61)
	return t ^ (t >> 14)
}

func (r *rng) below(n int) int {
	if n <= 0 {
		return 0
	}
	return int(r.next() % uint32(n))
}

func (r *rng) pick(items []string) string { return items[r.below(len(items))] }

func (r *rng) chance(percent int) bool { return r.below(100) < percent }

// trickyFragments exercise every scanner branch (the tricky set of the JS tests; its lone
// surrogate cannot be in a Go string and is U+FFFD here, plus a few invalid UTF-8 bytes).
var trickyFragments = []string{
	" ", "  ", "-", " - ", " – ", "—", "(", ")", "[", "]", "{", "}", "\"", "'", "“", "|", " | ",
	",", ";", "&", " & ", " x ", " feat. ", " ft ", "prod. by ", " by ", ".", ":", "_", "0", "1",
	"01. ", "3:45 ", "Remix", " Mix", "VIP", "Official Video", "ID", "\u200b", "\u00a0", "\u3000",
	"é", "e\u0301", "𝄞", "\ufffd", "×", "／", ".mp3", "\xff", "\xed\xa0\x80", "\xe2\x82",
}

// anyRune is one random code point, biased to the ranges the rules look at.
func (r *rng) anyRune() rune {
	var cp int
	switch r.below(10) {
	case 0, 1, 2, 3:
		cp = r.below(0x80)
	case 4:
		cp = r.below(0x100)
	case 5:
		cp = 0x300 + r.below(0x70) // combining marks
	case 6:
		cp = 0x2000 + r.below(0x70) // spaces, dashes, quotes, invisibles
	case 7:
		cp = r.below(0x10000)
	case 8:
		cp = 0xff00 + r.below(0xf0) // fullwidth forms
	default:
		cp = r.below(0x110000)
	}
	if cp >= 0xd800 && cp <= 0xdfff {
		return 0xfffd
	}
	return rune(cp)
}

func (r *rng) randomChars(max int) string {
	var b strings.Builder
	for n := r.below(max + 1); n > 0; n-- {
		b.WriteRune(r.anyRune())
	}
	return b.String()
}

func (r *rng) noisyString() string {
	var b strings.Builder
	for n := r.below(25); n > 0; n-- {
		if r.chance(50) {
			b.WriteString(r.pick(trickyFragments))
		} else {
			b.WriteString(r.randomChars(4))
		}
	}
	return b.String()
}

func (r *rng) anyString() string {
	if r.chance(50) {
		return r.randomChars(60)
	}
	return r.noisyString()
}

var (
	propArtists  = []string{"Noisia", "Phace", "Fox Stevenson", "Sub Focus", "Mefjus", "Camo & Krooked", "Alix Perez"}
	propTitles   = []string{"Tentacles", "Bruises", "Out the Blue", "Dead Limit", "Stigma", "Blind Faith"}
	propRemixers = []string{"Skrillex", "Magnetude", "Wilkinson", "Break"}
	propJoiners  = []string{" & ", ", ", " x ", " vs. "}
)

// trackString is a track-shaped input for the metamorphic and round-trip properties.
func (r *rng) trackString() string {
	var artists []string
	for n := 1 + r.below(3); n > 0; n-- {
		a := r.pick(propArtists)
		if !slices.Contains(artists, a) {
			artists = append(artists, a)
		}
	}
	s := strings.Join(artists, r.pick(propJoiners))
	if r.chance(50) {
		if feat := r.pick(propArtists); !slices.Contains(artists, feat) {
			s += " feat. " + feat
		}
	}
	s += " - " + r.pick(propTitles)
	if r.chance(50) {
		s += " (" + r.pick(propRemixers) + " Remix)"
	}
	return s
}

// quietTrack is a track-shaped input that parses without warnings.
func (r *rng) quietTrack() string {
	for {
		if s := r.trackString(); len(Parse(s).Warnings) == 0 {
			return s
		}
	}
}

// tracklistFragments exercise the line splitter, the time scanner, emphasis and the line
// kinds (the fragments of the JS and Rust tracklist property tests).
var tracklistFragments = []string{
	"\n", "\r\n", "\r", "\n\r", " ", "\u2028", "\u0085", "\u00a0", "  ", "\t", "\u3000", "00:00",
	"0:00 ", "04:37 ", "1:02:03 ", "01:02:03.5 ", "125:30 ", "12:34,250", "1:75 ", "0:61:00 ",
	"1:2", "[", "]", "(", ")", "[00:10] ", "(1:00)", " - ", " – ", "-", "|", ".", ":", "=> ", "==>",
	"1 ", "2 ", "01. ", "3) ", "Artist", "Title", "Noisia - Stigma", "Intro", "interview",
	"Tracklist:", "Part 2", "https://x.y", "www.", "_-_", "Ab-Cd", "ID", "(VIP)", "**", "*", "__",
	"_", "***", "* ", "- ", "• ", "**[", "]**", "w/ ", "W/", "\u200b", "𝄞", "🎵", "é", "Кино", "a",
	"7", "\x00", "\"", "\\", "\xff",
}

func (r *rng) noisyText() string {
	var b strings.Builder
	for n := r.below(41); n > 0; n-- {
		b.WriteString(r.pick(tracklistFragments))
	}
	return b.String()
}

var timeStyles = []string{"M:SS", "MM:SS", "H:MM:SS", "HH:MM:SS", "MM:SS.d", "M:SS,ddd"}

func writeTime(style string, seconds, frac int) string {
	h, m, s := seconds/3600, (seconds%3600)/60, seconds%60
	switch style {
	case "M:SS":
		return fmt.Sprintf("%d:%02d", seconds/60, s)
	case "MM:SS":
		return fmt.Sprintf("%02d:%02d", seconds/60, s)
	case "H:MM:SS":
		return fmt.Sprintf("%d:%02d:%02d", h, m, s)
	case "HH:MM:SS":
		return fmt.Sprintf("%02d:%02d:%02d", h, m, s)
	case "MM:SS.d":
		return fmt.Sprintf("%02d:%02d.%d", seconds/60, s, frac%10)
	}
	return fmt.Sprintf("%d:%02d,%03d", seconds/60, s, frac)
}

var listRests = []string{
	"Noisia - Stigma", "01. Phace - Plastic Acid", "2 Unlimited - No Limit", "Intro",
	"Paperclip interview", "Mefjus - Suicide Bassline (VIP)", "", "ID - ID",
}

func (r *rng) listLine() string {
	if r.below(7) == 0 {
		return r.pick([]string{"", "Tracklist:", "a note", "https://example.com"})
	}
	t := writeTime(r.pick(timeStyles), r.below(3600), r.below(1000))
	gap := r.pick([]string{" ", "  ", " - ", " | ", " => "})
	rest := r.pick(listRests)
	if lead := r.pick([]string{"", " ", "["}); lead != "[" {
		return lead + t + gap + rest
	}
	return "[" + t + "]" + gap + rest
}

// timedList is a realistic timed list for the edit properties.
func (r *rng) timedList() string {
	n := 1 + r.below(12)
	eol := r.pick([]string{"\n", "\r\n"})
	lines := make([]string, n)
	for i := range lines {
		lines[i] = r.listLine()
	}
	text := strings.Join(lines, eol)
	if r.chance(50) {
		text += eol
	}
	return text
}

func (r *rng) anyText() string {
	if r.chance(30) {
		return r.timedList()
	}
	return r.noisyText()
}

// ---------------------------------------------------------------------------------------------
// Helpers

// specSchemas is spec/schema, or nil when ../spec is absent (the module zip): the schema
// checks are then skipped, every other check still runs.
var specSchemas = sync.OnceValue(func() *schemaSet {
	s, err := loadSchemas(filepath.Join(specDir, "schema"))
	if err != nil {
		return nil
	}
	return s
})

// checkJSONSchema validates the JSON of an output against fn's schema (checkSchema).
func checkJSONSchema(tb testing.TB, fn string, out []byte, what string) {
	tb.Helper()
	s := specSchemas()
	if s == nil {
		return
	}
	v, err := parseJSON(out)
	if err != nil {
		tb.Fatalf("%s: invalid JSON %q", what, out)
	}
	if d := checkSchema(s, fn, v); d != "" {
		tb.Fatalf("%s: output violates the schema: %s\n%s", what, d, out)
	}
}

// trackJSON is the JSON of a track without its input, for comparisons.
func trackJSON(t Track) string {
	t.Input = ""
	return string(marshal(&t))
}

// trimmedWS reports whether s has no R0.2 whitespace at either end.
func trimmedWS(s string) bool {
	r := []rune(s)
	return len(r) == 0 || (!isWhitespace(r[0]) && !isWhitespace(r[len(r)-1]))
}

// ---------------------------------------------------------------------------------------------
// Single-track checks

// checkArtistHygiene is invariant 5: names are non-empty and trimmed, and no two credits share
// a dedup key (R9.2: producers are deduped only among producers).
func checkArtistHygiene(tb testing.TB, s string, track *Track) {
	tb.Helper()
	seen := map[string]bool{}
	for _, a := range track.Artists {
		if a.Name == "" || !trimmedWS(a.Name) {
			tb.Fatalf("%q: bad artist name %q", s, a.Name)
		}
		class := "c:"
		if a.Role == RoleProducer {
			class = "p:"
		}
		k := class + dedupKey(a.Name)
		if seen[k] {
			tb.Fatalf("%q: duplicate artist %q", s, a.Name)
		}
		seen[k] = true
	}
}

// checkTrackTotality is invariants 1, 2, 4 and 5 on any string: every public single-track
// function in every mode, with the string as its own options, returns; Parser and the
// one-shot functions agree; the output is schema-valid; normalize is idempotent.
func checkTrackTotality(tb testing.TB, s string) {
	tb.Helper()
	clean := sanitize(s)
	def := Parse(s)
	if def.Input != clean {
		tb.Fatalf("%q: input %q is not the sanitized input", s, def.Input)
	}
	checkJSONSchema(tb, "parse", marshal(&def), fmt.Sprintf("parse(%q)", s))
	checkArtistHygiene(tb, s, &def)
	_ = Format(def)
	_ = AllArtists(def)
	artists := ParseArtists(s)
	checkJSONSchema(tb, "parseArtists", marshal(artistList(artists)), fmt.Sprintf("parseArtists(%q)", s))
	once := Normalize(s)
	if twice := Normalize(once); twice != once {
		tb.Fatalf("normalize is not idempotent on %q: %q then %q", s, once, twice)
	}
	for _, mode := range append([]Mode{ModeAuto, Mode("bogus")}, AllModes()...) {
		options := ParseOptions{
			Mode:         mode,
			Uploader:     s,
			KnownArtists: []string{s, "a", ""},
			SplitAnd:     SplitAndAlways,
		}
		p := NewParser(options)
		track := p.Parse(s)
		if track.Input != clean {
			tb.Fatalf("%q in %q: input %q", s, mode, track.Input)
		}
		if a, b := string(marshal(&track)), func() string { t := ParseWith(s, options); return string(marshal(&t)) }(); a != b {
			tb.Fatalf("%q in %q: Parser and ParseWith differ:\n%s\n%s", s, mode, a, b)
		}
		checkJSONSchema(tb, "parse", marshal(&track), fmt.Sprintf("parse(%q, %q)", s, mode))
		checkArtistHygiene(tb, s, &track)
		if a, b := string(marshal(artistList(p.ParseArtists(s)))), string(marshal(artistList(ParseArtistsWith(s, options)))); a != b {
			tb.Fatalf("%q in %q: Parser.ParseArtists and ParseArtistsWith differ", s, mode)
		}
		for _, feat := range []FeatPlacement{"", FeatSource, FeatArtist, FeatTitle, FeatOmit, "nope"} {
			_ = FormatWith(track, FormatOptions{
				Feat: feat, FeatMarker: &s, Joiners: JoinersCanonical, Position: true,
			})
		}
	}
}

// formatOptionsFrom is a format option set chosen by r, odd values included.
func formatOptionsFrom(r *rng, s string) FormatOptions {
	return FormatOptions{
		Feat:          FeatPlacement(r.pick([]string{"", "source", "artist", "title", "omit", "x"})),
		FeatMarker:    []*string{nil, nil, Ptr(""), Ptr("feat."), Ptr("ft."), Ptr("&"), &s}[r.below(7)],
		Joiners:       JoinerStyle(r.pick([]string{"", "original", "canonical", "x"})),
		Versions:      VersionsOption(r.pick([]string{"", "all", "none", "x"})),
		OmitProducers: r.chance(30),
		Position:      r.chance(50),
	}
}

// checkNoiseInsensitive is invariant 3 on a track-shaped input: doubled spaces, dash variants
// and a zero-width space change nothing but the input.
func checkNoiseInsensitive(tb testing.TB, s string, cut int) {
	tb.Helper()
	base := trackJSON(Parse(s))
	rs := []rune(s)
	cut %= len(rs) + 1
	noisy := []string{
		strings.ReplaceAll(s, " ", "  "),
		strings.ReplaceAll(s, " - ", " – "),
		strings.ReplaceAll(s, " - ", " — "),
		string(rs[:cut]) + "\u200b" + string(rs[cut:]),
	}
	for _, n := range noisy {
		if got := trackJSON(Parse(n)); got != base {
			tb.Fatalf("noise changed the parse of %q (%q):\n%s\n%s", s, n, base, got)
		}
	}
}

// shape is what R11.6 promises to keep: names with roles, the title, and each version's type
// and names.
func shape(t *Track) string {
	var b strings.Builder
	for _, a := range t.Artists {
		fmt.Fprintf(&b, "%s/%s;", a.Name, a.Role)
	}
	fmt.Fprintf(&b, "|%s|", t.Title)
	for _, v := range t.Versions {
		fmt.Fprintf(&b, "%s:", v.Type)
		for _, a := range v.Artists {
			fmt.Fprintf(&b, "%s,", a.Name)
		}
		b.WriteString(";")
	}
	return b.String()
}

// checkRoundTrip is invariant 8 (R11.6) on a warning-free track-shaped input.
func checkRoundTrip(tb testing.TB, s string) {
	tb.Helper()
	first := Parse(s)
	if len(first.Warnings) > 0 {
		return
	}
	formatted := Format(first)
	second := Parse(formatted)
	if a, b := shape(&first), shape(&second); a != b {
		tb.Fatalf("round trip %q -> %q:\n%s\n%s", s, formatted, a, b)
	}
}

// contentWords is invariant 6: the words of normalize(input), minus separators, joiners, feat
// markers and bracket characters.
func contentWords(normalized string) []string {
	var out []string
	for _, w := range strings.FieldsFunc(normalized, isWhitespace) {
		switch w {
		case "-", "&", "x", "vs.", "feat.":
			continue
		}
		w = strings.Trim(w, "()[],.")
		if w != "" {
			out = append(out, w)
		}
	}
	return out
}

func outputStrings(t *Track) []string {
	out := []string{t.Title, t.FullTitle}
	for _, a := range t.Artists {
		out = append(out, a.Name)
	}
	for _, v := range t.Versions {
		out = append(out, v.Raw)
		for _, a := range v.Artists {
			out = append(out, a.Name)
		}
	}
	for _, j := range t.Junk {
		out = append(out, j.Raw)
	}
	if t.Position != nil {
		out = append(out, t.Position.Raw)
	}
	if t.Timestamp != nil {
		out = append(out, t.Timestamp.Raw)
	}
	return out
}

// checkConservation is invariant 6 ("no word goes missing") on a track-shaped input.
func checkConservation(tb testing.TB, s string) {
	tb.Helper()
	track := Parse(s)
	fields := outputStrings(&track)
	for _, w := range contentWords(Normalize(s)) {
		if !slices.ContainsFunc(fields, func(f string) bool { return strings.Contains(f, w) }) {
			tb.Fatalf("%q of %q is in no output field: %s", w, s, marshal(&track))
		}
	}
}

// decorated is a track-shaped input with an optional position and junk group (invariant 6).
func (r *rng) decorated() string {
	s := r.trackString()
	if r.chance(30) {
		s = r.pick([]string{"01. ", "7. ", "12. ", "[03:45] ", "1:02:03 "}) + s
	}
	if r.chance(30) {
		s += r.pick([]string{" (Official Video)", " [HD]", " [Free Download]", " (VIP)"})
	}
	return s
}

// checkMetamorphic is invariant 7 on a warning-free track-shaped input.
func checkMetamorphic(tb testing.TB, s string) {
	tb.Helper()
	plain := Parse(s)
	if len(plain.Warnings) > 0 {
		return
	}
	// Prefixing `01. ` only sets the position.
	numbered := Parse("01. " + s)
	if numbered.Position == nil || *numbered.Position != (Position{Raw: "01", Number: 1}) {
		tb.Fatalf("01. %s: position %+v", s, numbered.Position)
	}
	numbered.Position = nil
	if a, b := trackJSON(plain), trackJSON(numbered); a != b {
		tb.Fatalf("01. changed more than the position of %q:\n%s\n%s", s, a, b)
	}
	// Appending ` (Official Video)` only adds junk (and may switch the mode to youtube).
	noisy := Parse(s + " (Official Video)")
	want := append(append([]Junk{}, plain.Junk...), Junk{Raw: "Official Video", Kind: JunkKindVideo})
	if !slices.Equal(noisy.Junk, want) {
		tb.Fatalf("%q (Official Video): junk %+v", s, noisy.Junk)
	}
	if noisy.Mode != plain.Mode && noisy.Mode != ModeYouTube {
		tb.Fatalf("%q (Official Video): mode %q", s, noisy.Mode)
	}
	noisy.Junk, noisy.Mode = plain.Junk, plain.Mode
	if a, b := trackJSON(plain), trackJSON(noisy); a != b {
		tb.Fatalf("(Official Video) changed more than the junk of %q:\n%s\n%s", s, a, b)
	}
	if !strings.HasSuffix(s, " Remix)") {
		return
	}
	open := strings.LastIndexByte(s, '(')
	head, group := s[:open], s[open+1:len(s)-1]
	// (X Remix) and [X Remix] differ only in the delimiter.
	square := Parse(head + "[" + group + "]")
	if n := len(square.Versions); n == 0 || square.Versions[n-1].Delimiter != DelimiterSquare {
		tb.Fatalf("%q: no square version", s)
	}
	for i := range square.Versions {
		square.Versions[i].Delimiter = DelimiterParen
	}
	square.FullTitle = plain.FullTitle
	if a, b := trackJSON(plain), trackJSON(square); a != b {
		tb.Fatalf("[ ] changed more than the delimiter of %q:\n%s\n%s", s, a, b)
	}
	// REMIX and Remix differ only in raw.
	shouted := strings.ReplaceAll(group, " Remix", " REMIX")
	upper := Parse(head + "(" + shouted + ")")
	if n := len(upper.Versions); n == 0 || upper.Versions[n-1].Raw != shouted {
		tb.Fatalf("%q: REMIX raw %+v", s, upper.Versions)
	}
	for i := range upper.Versions {
		if i < len(plain.Versions) {
			upper.Versions[i].Raw = plain.Versions[i].Raw
		}
	}
	upper.FullTitle = plain.FullTitle
	if a, b := trackJSON(plain), trackJSON(upper); a != b {
		tb.Fatalf("REMIX changed more than raw in %q:\n%s\n%s", s, a, b)
	}
}

// checkShapedTrack runs every track-shaped check on inputs drawn from r.
func checkShapedTrack(tb testing.TB, r *rng) {
	tb.Helper()
	s := r.trackString()
	checkNoiseInsensitive(tb, s, r.below(1000))
	checkRoundTrip(tb, s)
	checkMetamorphic(tb, s)
	checkConservation(tb, r.decorated())
	track := Parse(s)
	_ = FormatWith(track, formatOptionsFrom(r, s))
}

// ---------------------------------------------------------------------------------------------
// Tracklist checks

func rawLines(l *Tracklist) []string {
	out := make([]string, len(l.Entries))
	for i, e := range l.Entries {
		out[i] = e.Raw + string(e.Eol)
	}
	return out
}

func raws(text string) []string {
	l := ParseTracklist(text)
	out := make([]string, len(l.Entries))
	for i, e := range l.Entries {
		out[i] = e.Raw
	}
	return out
}

// checkTracklist is R13.11 invariants 1 and 2 on any text: lossless, parts concatenate to raw,
// line numbering and terminators, the kind/track pairing, schema-valid output.
func checkTracklist(tb testing.TB, text string, options TracklistOptions) {
	tb.Helper()
	clean := sanitize(text)
	l := ParseTracklistWith(text, options)
	if got := strings.Join(rawLines(&l), ""); got != clean {
		tb.Fatalf("not lossless: %q gives %q", text, got)
	}
	checkJSONSchema(tb, "parseTracklist", marshal(&l), fmt.Sprintf("parseTracklist(%q)", text))
	if l.Stats.Lines != len(l.Entries) {
		tb.Fatalf("%q: stats.lines %d, %d entries", text, l.Stats.Lines, len(l.Entries))
	}
	for i, e := range l.Entries {
		p := e.Parts
		if p.Lead+p.Time+p.Gap+p.Rest != e.Raw {
			tb.Fatalf("%q line %d: parts %+v do not join to %q", text, i+1, p, e.Raw)
		}
		// R13.1.3: without a time, `lead` holds at most a bullet; `time` and `gap` are empty.
		if (e.Time == nil) != (p.Time == "") || (e.Time == nil && p.Gap != "") {
			tb.Fatalf("%q line %d: time %+v with parts %+v", text, i+1, e.Time, p)
		}
		if (e.Track != nil) != (e.Kind == EntryKindTrack || e.Kind == EntryKindMarker) {
			tb.Fatalf("%q line %d: kind %q with track %v", text, i+1, e.Kind, e.Track != nil)
		}
		if strings.ContainsAny(e.Raw, "\r\n") {
			tb.Fatalf("%q line %d: raw %q holds a line break", text, i+1, e.Raw)
		}
		if e.Line != i+1 {
			tb.Fatalf("%q: entry %d has line %d", text, i, e.Line)
		}
		// Only the last line may lack a terminator.
		if e.Eol == EolNone && i != len(l.Entries)-1 {
			tb.Fatalf("%q line %d: no terminator before the last line", text, i+1)
		}
	}
}

// looseEdits is a random edit list for the JSON door: every field may be anything.
func (r *rng) looseEdits() jsonValue {
	fields := []string{
		"-3", "0", "1", "2", "5", "15", "1.5", "-0.25", "1e300", "-1e300", "1e999", "-1e999",
		"null", "true", "false", `"x"`, `""`, "[1,2]", "{}", "9007199254740993", "359999.5",
	}
	ops := []string{"set-time", "shift", "sort-by-time", "set-rest", "insert", "delete", "x"}
	texts := []string{`"a\nb"`, `"c\rd"`, `"00:10 New - Line"`, `"w/ x"`, `""`, `"\ud800"`}
	var edits []string
	for n := r.below(7); n > 0; n-- {
		if r.chance(8) {
			edits = append(edits, r.pick([]string{"null", "5", `"shift"`, "[]", "{}"}))
			continue
		}
		parts := []string{fmt.Sprintf(`"op":%q`, r.pick(ops))}
		for _, key := range []string{"line", "seconds", "from", "after"} {
			if r.chance(60) {
				parts = append(parts, fmt.Sprintf("%q:%s", key, r.pick(fields)))
			}
		}
		if r.chance(30) {
			var items []string
			for k := r.below(4); k > 0; k-- {
				items = append(items, r.pick(fields))
			}
			parts = append(parts, `"lines":[`+strings.Join(items, ",")+"]")
		}
		for _, key := range []string{"rest", "text"} {
			if r.chance(60) {
				value := r.pick(fields)
				if r.chance(80) {
					value = r.pick(texts)
				}
				parts = append(parts, fmt.Sprintf("%q:%s", key, value))
			}
		}
		edits = append(edits, "{"+strings.Join(parts, ",")+"}")
	}
	v, err := parseJSON([]byte("[" + strings.Join(edits, ",") + "]"))
	if err != nil {
		panic(err) // a bug in the generator
	}
	return v
}

// checkLooseEdits is R13.11 invariant 2 for edits: any edit list through the JSON door returns
// a schema-valid result whose changed lines are sorted, unique and in range, and whose text
// parses losslessly.
func checkLooseEdits(tb testing.TB, text string, edits jsonValue) {
	tb.Helper()
	clean := sanitize(text)
	out := wireCall("applyEdits", clean, jsonValue{kind: jsonObject}, jsonValue{kind: jsonObject}, edits)
	what := fmt.Sprintf("applyEdits(%q, %s)", text, marshal(edits))
	checkJSONSchema(tb, "applyEdits", out, what)
	v, err := parseJSON(out)
	if err != nil {
		tb.Fatalf("%s: invalid JSON %q", what, out)
	}
	newText, _ := v.getString("text")
	n := len(ParseTracklist(newText).Entries)
	changed, _ := v.get("changed")
	prev := 0.0
	for _, c := range changed.arr {
		if c.kind != jsonNumber || c.num <= prev || c.num > float64(n) {
			tb.Fatalf("%s: changed %s with %d lines", what, marshal(changed), n)
		}
		prev = c.num
	}
	if l := ParseTracklist(newText); strings.Join(rawLines(&l), "") != newText {
		tb.Fatalf("%s: the result does not parse losslessly", what)
	}
}

func warned(r *EditResult, codes ...EditWarningCode) bool {
	return slices.ContainsFunc(r.Warnings, func(w EditWarning) bool { return slices.Contains(codes, w.Code) })
}

// checkIdentity is R13.11 invariant 3: no edits return the text unchanged.
func checkIdentity(tb testing.TB, text string) {
	tb.Helper()
	for _, edits := range [][]Edit{nil, {}} {
		r := ApplyEdits(text, edits)
		if r.Text != sanitize(text) || len(r.Changed) != 0 || len(r.Warnings) != 0 {
			tb.Fatalf("applyEdits(%q, none) = %+v", text, r)
		}
	}
}

// checkLocality is R13.11 invariant 3: an edit on one line leaves every other line byte for
// byte, and changes only that line.
func checkLocality(tb testing.TB, text string, r *rng) {
	tb.Helper()
	text = sanitize(text)
	before := ParseTracklist(text)
	lines := rawLines(&before)
	if len(lines) == 0 {
		return
	}
	line := 1 + r.below(len(lines))
	seconds := float64(r.below(8001) - 4000)
	var edit Edit
	switch r.below(3) {
	case 0:
		edit = SetTime(line, seconds)
	case 1:
		edit = SetRest(line, "New - Rest")
	default:
		edit = ShiftLines(seconds, line)
	}
	res := ApplyEdits(text, []Edit{edit})
	afterList := ParseTracklist(res.Text)
	after := rawLines(&afterList)
	if len(after) != len(lines) {
		tb.Fatalf("%+v on %q: %d lines became %d", edit, text, len(lines), len(after))
	}
	for i := range after {
		if i != line-1 && after[i] != lines[i] {
			tb.Fatalf("%+v on %q: line %d changed: %q -> %q", edit, text, i+1, lines[i], after[i])
		}
	}
	for _, c := range res.Changed {
		if c != line {
			tb.Fatalf("%+v on %q: changed %v", edit, text, res.Changed)
		}
	}
	if warned(&res, EditWarningCodeEditInvalid) && (res.Text != text || len(res.Changed) != 0) {
		tb.Fatalf("%+v on %q: an invalid edit changed the text", edit, text)
	}
}

// checkInsertDelete is R13.11 invariant 3 for insert and delete: the other lines stay.
func checkInsertDelete(tb testing.TB, text string, r *rng) {
	tb.Helper()
	text = sanitize(text)
	before := raws(text)
	if len(before) == 0 || hasBareCR(text) {
		// R13.9.5: after an edit the lines are re-read, so a line ending in a bare "\r" next
		// to an empty "\n" line merges with it. The simple expectation below does not model
		// that.
		return
	}
	at := r.below(len(before) + 1)
	const newLine = "00:00 New - Line"
	ins := ApplyEdits(text, []Edit{Insert(at, newLine)})
	expected := slices.Insert(slices.Clone(before), at, newLine)
	if got := raws(ins.Text); !slices.Equal(got, expected) {
		tb.Fatalf("insert after %d in %q: %q, want %q", at, text, got, expected)
	}
	if !slices.Contains(ins.Changed, at+1) {
		tb.Fatalf("insert after %d in %q: changed %v", at, text, ins.Changed)
	}
	line := 1 + r.below(len(before))
	del := ApplyEdits(text, []Edit{Delete(line)})
	kept := slices.Delete(slices.Clone(before), line-1, line)
	got := raws(del.Text)
	// Deleting the last line moves its eol ("" here) up: an empty line that becomes last
	// without an eol is not a line (R13.9.5), so trailing empty lines may disappear.
	for len(kept) > 0 && kept[len(kept)-1] == "" && len(got) < len(kept) && line == len(before) {
		kept = kept[:len(kept)-1]
	}
	if !slices.Equal(got, kept) {
		tb.Fatalf("delete %d in %q: %q, want %q", line, text, got, kept)
	}
}

// hasBareCR reports whether text has a "\r" that is not part of a "\r\n".
func hasBareCR(text string) bool {
	for i := 0; i < len(text); i++ {
		if text[i] == '\r' && (i+1 >= len(text) || text[i+1] != '\n') {
			return true
		}
	}
	return false
}

func timeWidths(text string) []int {
	l := ParseTracklist(text)
	out := make([]int, len(l.Entries))
	for i, e := range l.Entries {
		out[i] = -1
		if e.Time != nil {
			out[i] = len(e.Time.Raw)
		}
	}
	return out
}

// checkShiftRoundTrip is R13.11 invariant 4: shift(+s) then shift(-s) restores the text,
// unless a time was clamped or upgraded or its digit count grew (R13.9.7). It reports whether
// the round trip was checked.
func checkShiftRoundTrip(tb testing.TB, text string, r *rng) bool {
	tb.Helper()
	text = sanitize(text)
	s := float64(1 + r.below(5000))
	n := len(ParseTracklist(text).Entries)
	shift := func(seconds float64) Edit { return Shift(seconds) }
	if from := 1 + r.below(12); r.chance(50) && from <= n {
		shift = func(seconds float64) Edit { return ShiftFrom(seconds, from) }
	}
	up := ApplyEdits(text, []Edit{shift(s)})
	down := ApplyEdits(up.Text, []Edit{shift(-s)})
	lossy := []EditWarningCode{EditWarningCodeTimeClamped, EditWarningCodeStyleUpgraded, EditWarningCodeEditInvalid}
	if warned(&up, lossy...) || warned(&down, lossy...) || !slices.Equal(timeWidths(up.Text), timeWidths(text)) {
		return false
	}
	if down.Text != text {
		tb.Fatalf("shift ±%v on %q gives %q", s, text, down.Text)
	}
	return true
}

// checkSort is R13.11 invariant 4 for sort-by-time: the eol slots stay, times ascend, and a
// second sort changes nothing.
func checkSort(tb testing.TB, text string) {
	tb.Helper()
	text = sanitize(text)
	eols := func(text string) []Eol {
		l := ParseTracklist(text)
		out := make([]Eol, len(l.Entries))
		for i, e := range l.Entries {
			out[i] = e.Eol
		}
		return out
	}
	once := ApplyEdits(text, []Edit{SortByTime()})
	// An empty line sorted into the last slot (eol "") is not a line any more (R13.1.1).
	if a, b := eols(once.Text), eols(text); len(a) == len(b) && !slices.Equal(a, b) {
		tb.Fatalf("sort of %q moved the terminators: %q", text, once.Text)
	}
	prev := -1.0
	for _, e := range ParseTracklist(once.Text).Entries {
		if e.Time != nil {
			if e.Time.Seconds < prev {
				tb.Fatalf("sort of %q is not sorted: %q", text, once.Text)
			}
			prev = e.Time.Seconds
		}
	}
	if twice := ApplyEdits(once.Text, []Edit{SortByTime()}); twice.Text != once.Text {
		tb.Fatalf("sort of %q is not idempotent", text)
	}
}

// checkOneLine is R13.11 invariant 5: the track of a one-line text is the parse of its line
// text in tracklist mode. It reports whether the line had a track.
func checkOneLine(tb testing.TB, text string) bool {
	tb.Helper()
	text = strings.NewReplacer("\r", " ", "\n", " ").Replace(sanitize(text))
	l := ParseTracklist(text)
	if len(l.Entries) == 0 {
		return false
	}
	if len(l.Entries) != 1 {
		tb.Fatalf("%q: %d entries", text, len(l.Entries))
	}
	e := l.Entries[0]
	if e.Track == nil {
		return false
	}
	want := ParseWith(e.Track.Input, ParseOptions{Mode: ModeTracklist})
	if a, b := string(marshal(e.Track)), string(marshal(&want)); a != b {
		tb.Fatalf("%q: the entry's track is not the parse of its text:\n%s\n%s", text, a, b)
	}
	if !strings.ContainsAny(e.Parts.Rest, "*_") && !e.Layered && e.Track.Input != e.Parts.Rest {
		tb.Fatalf("%q: track input %q, rest %q", text, e.Track.Input, e.Parts.Rest)
	}
	return true
}

var emphasisRests = []string{
	"Noisia - Stigma", "01. Phace - Plastic Acid", "Intro", "Paperclip interview",
	"Mefjus - Suicide Bassline (VIP)", "ID - ID", "Noisia feat. Foreign Beggars - Shellshock",
}

var timeWraps = [][2]string{
	{"", ""}, {"**", "**"}, {"__", "__"}, {"*", "*"}, {"_", "_"}, {"**[", "]**"}, {"[**", "**]"},
	{"**[**", "**]"}, {"_[", "]_"},
}

// checkEmphasis is R13.11 invariant 6: emphasis around the time or the track part changes
// neither the time, the kind nor the track.
func checkEmphasis(tb testing.TB, t, rest string) {
	tb.Helper()
	bare := ParseTracklist(t + " " + rest).Entries[0]
	for _, w := range timeWraps {
		for _, wrap := range []string{"", "**", "__", "*", "_"} {
			decorated := w[0] + t + w[1] + " " + wrap + rest + wrap
			e := ParseTracklist(decorated).Entries[0]
			if (e.Time == nil) != (bare.Time == nil) || (e.Time != nil && e.Time.Seconds != bare.Time.Seconds) {
				tb.Fatalf("%q: time %+v, want %+v", decorated, e.Time, bare.Time)
			}
			if e.Kind != bare.Kind {
				tb.Fatalf("%q: kind %q, want %q", decorated, e.Kind, bare.Kind)
			}
			if (e.Track == nil) != (bare.Track == nil) ||
				(e.Track != nil && string(marshal(e.Track)) != string(marshal(bare.Track))) {
				tb.Fatalf("%q: track differs from %q", decorated, t+" "+rest)
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Seeded property tests

// propertyRuns scales the iteration counts down under -short and -race.
func propertyRuns(n int) int {
	switch {
	case testing.Short():
		return n / 10
	case raceEnabled:
		return n / 4
	}
	return n
}

func TestPropertyTotality(t *testing.T) {
	r := newRNG(1)
	for i := 0; i < propertyRuns(2000); i++ {
		checkTrackTotality(t, r.anyString())
	}
	for _, s := range []string{"", " ", "\x00", "\ufeff", "-", " - ", "(", ")", "((((", "]]]]", "\U0010ffff", "\xff\xfe"} {
		checkTrackTotality(t, s)
	}
	for _, f := range trickyFragments {
		checkTrackTotality(t, f)
		checkTrackTotality(t, strings.Repeat(f, 50))
	}
}

func TestPropertyNormalizeIdempotent(t *testing.T) {
	r := newRNG(2)
	for i := 0; i < propertyRuns(4000); i++ {
		s := r.anyString()
		once := Normalize(s)
		if twice := Normalize(once); twice != once {
			t.Fatalf("case %d: %q: %q then %q", i, s, once, twice)
		}
	}
}

func TestPropertyNoiseInsensitive(t *testing.T) {
	r := newRNG(3)
	for i := 0; i < propertyRuns(500); i++ {
		checkNoiseInsensitive(t, r.trackString(), r.below(1000))
	}
}

func TestPropertyArtistHygiene(t *testing.T) {
	r := newRNG(5)
	for i := 0; i < propertyRuns(3000); i++ {
		s := r.trackString()
		if r.chance(70) {
			s = r.anyString()
		}
		track := Parse(s)
		checkArtistHygiene(t, s, &track)
	}
}

func TestPropertyConservation(t *testing.T) {
	r := newRNG(6)
	for i := 0; i < propertyRuns(1000); i++ {
		checkConservation(t, r.decorated())
	}
}

func TestPropertyMetamorphic(t *testing.T) {
	r := newRNG(7)
	for i := 0; i < propertyRuns(500); i++ {
		checkMetamorphic(t, r.quietTrack())
	}
}

func TestPropertyRoundTrip(t *testing.T) {
	r := newRNG(11)
	for i := 0; i < propertyRuns(500); i++ {
		s := r.quietTrack()
		checkRoundTrip(t, s)
		_ = FormatWith(Parse(s), formatOptionsFrom(r, s))
	}
}

func TestPropertyTracklistLossless(t *testing.T) {
	r := newRNG(21)
	for i := 0; i < propertyRuns(3000); i++ {
		checkTracklist(t, r.anyText(), TracklistOptions{})
	}
}

func TestPropertyOnlyLFCRLFAndCRBreakLines(t *testing.T) {
	for text, want := range map[string]int{
		"a\u2028b\u2029c\u0085d\u000be\u000cf": 1,
		"a\r\nb\rc\nd\n":                       4,
		"\n\r\r\n":                             3,
		"":                                     0,
	} {
		if got := len(ParseTracklist(text).Entries); got != want {
			t.Errorf("%q: %d lines, want %d", text, got, want)
		}
	}
}

func TestPropertyAnyEditListReturns(t *testing.T) {
	r := newRNG(22)
	for i := 0; i < propertyRuns(2000); i++ {
		checkLooseEdits(t, r.anyText(), r.looseEdits())
	}
}

func TestPropertyNoEditsIsTheIdentity(t *testing.T) {
	r := newRNG(23)
	for i := 0; i < propertyRuns(1000); i++ {
		checkIdentity(t, r.anyText())
	}
}

func TestPropertyEditLocality(t *testing.T) {
	r := newRNG(24)
	for i := 0; i < propertyRuns(1500); i++ {
		text := r.timedList()
		if r.chance(20) {
			text = r.noisyText()
		}
		checkLocality(t, text, r)
		checkInsertDelete(t, r.timedList(), r)
	}
}

func TestPropertyShiftRoundTrip(t *testing.T) {
	r := newRNG(26)
	checked := 0
	for i := 0; i < propertyRuns(2000); i++ {
		if checkShiftRoundTrip(t, r.timedList(), r) {
			checked++
		}
	}
	if checked < propertyRuns(2000)/20 {
		t.Fatalf("only %d round trips were checked", checked)
	}
}

func TestPropertySort(t *testing.T) {
	r := newRNG(27)
	for i := 0; i < propertyRuns(1500); i++ {
		checkSort(t, r.timedList())
	}
}

func TestPropertyOneLineTrack(t *testing.T) {
	r := newRNG(28)
	tracks := 0
	for i := 0; i < propertyRuns(3000); i++ {
		text := ""
		if r.chance(50) {
			text = r.listLine()
		}
		if checkOneLine(t, text+r.noisyText()) {
			tracks++
		}
	}
	if tracks < propertyRuns(3000)/6 {
		t.Fatalf("only %d one-line tracks were checked", tracks)
	}
}

func TestPropertyEmphasis(t *testing.T) {
	r := newRNG(29)
	for i := 0; i < propertyRuns(40); i++ {
		tm := writeTime(r.pick(timeStyles), r.below(3600), r.below(1000))
		for _, rest := range emphasisRests {
			checkEmphasis(t, tm, rest)
		}
	}
}
