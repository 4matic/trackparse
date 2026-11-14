package trackparse

import (
	"fmt"
	"math"
	"math/rand"
	"strings"
	"testing"
)

// Unit tests for the tracklist modules (R13) that do not need the single-track pipeline:
// split, the time scanner and time formatting, emphasis, positions, warnings and the
// suggestion search, and edits. Expected values are the JS reference's output.

func TestSplitLines(t *testing.T) {
	cases := []struct {
		text string
		want []tlLine
	}{
		{"", nil},
		{"\n", []tlLine{{"", EolLF}}},
		{"a", []tlLine{{"a", EolNone}}},
		{"a\nb", []tlLine{{"a", EolLF}, {"b", EolNone}}},
		{"a\r\nb\r\n", []tlLine{{"a", EolCRLF}, {"b", EolCRLF}}},
		{"a\rb\r", []tlLine{{"a", EolCR}, {"b", EolCR}}},
		{"a\r\r\nb\n\n", []tlLine{{"a", EolCR}, {"", EolCRLF}, {"b", EolLF}, {"", EolLF}}},
		{"\n\r", []tlLine{{"", EolLF}, {"", EolCR}}},
		// R0.2 whitespace that is not a line break.
		{"a\u2028b\u2029c\u0085d\v\fe", []tlLine{{"a\u2028b\u2029c\u0085d\v\fe", EolNone}}},
		{"é\n🎵\r\n", []tlLine{{"é", EolLF}, {"🎵", EolCRLF}}},
	}
	for _, c := range cases {
		got := splitLines(c.text)
		if fmt.Sprint(got) != fmt.Sprint(c.want) {
			t.Errorf("splitLines(%q) = %q, want %q", c.text, got, c.want)
		}
		// R13.1.2: lossless.
		var b strings.Builder
		for _, l := range got {
			b.WriteString(l.raw + string(l.eol))
		}
		if b.String() != c.text {
			t.Errorf("splitLines(%q) joins to %q", c.text, b.String())
		}
	}
	if !hasLineBreak("a\rb") || !hasLineBreak("a\n") || hasLineBreak("a\u2028b\u0085") {
		t.Error("hasLineBreak")
	}
}

func TestScanTime(t *testing.T) {
	type want struct {
		lead, time, gap, rest string
		seconds               float64 // -1: no time
		invalid               bool
		text                  string
	}
	cases := []struct {
		line string
		want want
	}{
		{"00:00 Noisia - Stigma", want{"", "00:00", " ", "Noisia - Stigma", 0, false, "Noisia - Stigma"}},
		{"[03:45] Phace - Desert", want{"[", "03:45", "] ", "Phace - Desert", 225, false, "Phace - Desert"}},
		{"(1:02:03) A - B", want{"(", "1:02:03", ") ", "A - B", 3723, false, "A - B"}},
		{"  - 52:47 - A - B", want{"  - ", "52:47", " - ", "A - B", 3167, false, "A - B"}},
		{"125:30 A", want{"", "125:30", " ", "A", 7530, false, "A"}},
		{"00:00.5 A", want{"", "00:00.5", " ", "A", 0.5, false, "A"}},
		{"12:34,250 x", want{"", "12:34,250", " ", "x", 754.25, false, "x"}},
		// Four fraction digits are not a fraction: "." is the gap.
		{"12:34.5678 x", want{"", "12:34", ".", "5678 x", 754, false, "5678 x"}},
		{"01:02:03.250 A", want{"", "01:02:03.250", " ", "A", 3723.25, false, "A"}},
		{"1:75 x - y", want{"", "", "", "1:75 x - y", -1, true, "1:75 x - y"}},
		{"1:2:3:4 x", want{"", "", "", "1:2:3:4 x", -1, true, "1:2:3:4 x"}},
		{"100:00:00 x", want{"", "", "", "100:00:00 x", -1, true, "100:00:00 x"}},
		{"1000:00 x", want{"", "", "", "1000:00 x", -1, true, "1000:00 x"}},
		{"00:00x", want{"", "", "", "00:00x", -1, false, "00:00x"}},
		{"00:00", want{"", "00:00", "", "", 0, false, ""}},
		{"00:00 => A", want{"", "00:00", " => ", "A", 0, false, "A"}},
		{"00:00 ==> A", want{"", "00:00", " ==> ", "A", 0, false, "A"}},
		{"00:00 | A", want{"", "00:00", " | ", "A", 0, false, "A"}},
		{"**00:00** *Noisia* - Stigma", want{"**", "00:00", "** ", "*Noisia* - Stigma", 0, false, "Noisia - Stigma"}},
		{"**[00:00]** A", want{"**[", "00:00", "]** ", "A", 0, false, "A"}},
		{"[**00:00**] A", want{"[**", "00:00", "**] ", "A", 0, false, "A"}},
		// R13.12.4: a lead run the gap leaves open pairs with a run at the end of the line.
		{"**00:00 A - B**", want{"**", "00:00", " ", "A - B**", 0, false, "A - B"}},
		{"- untimed line", want{"- ", "", "", "untimed line", -1, false, "untimed line"}},
		{"-not a bullet", want{"", "", "", "-not a bullet", -1, false, "-not a bullet"}},
		{"\u3000• 01:30\u00a0X", want{"\u3000• ", "01:30", "\u00a0", "X", 90, false, "X"}},
		{"[00:00 A", want{"", "", "", "[00:00 A", -1, false, "[00:00 A"}},
	}
	for _, c := range cases {
		s := scanTime(c.line)
		p := s.parts
		got := want{p.Lead, p.Time, p.Gap, p.Rest, -1, s.invalid, s.text}
		if s.time != nil {
			got.seconds = s.time.Seconds
			if s.time.Raw != p.Time {
				t.Errorf("%q: time raw %q, parts.time %q", c.line, s.time.Raw, p.Time)
			}
		}
		if got != c.want {
			t.Errorf("scanTime(%q) =\n %+v, want\n %+v", c.line, got, c.want)
		}
		if p.Lead+p.Time+p.Gap+p.Rest != c.line {
			t.Errorf("scanTime(%q): parts are not lossless", c.line)
		}
	}
}

func TestSecondsOf(t *testing.T) {
	// One decimal literal, correctly rounded (JS Number()).
	if secondsOf(754, "25") != 754.25 || secondsOf(1, "005") != 1.005 || secondsOf(3167, "") != 3167 {
		t.Error("secondsOf")
	}
}

func styleOf(t *testing.T, raw string) *timeStyle {
	t.Helper()
	s := scanTime(raw + " x")
	if s.style == nil {
		t.Fatalf("no time in %q", raw)
	}
	return s.style
}

func TestFormatTime(t *testing.T) {
	cases := []struct {
		style    string
		seconds  float64
		oneDigit bool
		want     string // "" when it cannot be written
		upgraded bool
	}{
		{"00:00", 225, false, "03:45", false},
		{"0:00", 225, false, "3:45", false},
		{"000:00", 5, false, "000:05", false},
		{"59:30", 3615, false, "01:00:15", true},
		{"59:30", 3615, true, "1:00:15", true},
		{"1:00:00", 3725, false, "1:02:05", false},
		{"01:00:00", 5, false, "00:00:05", false},
		// Long-minute style stays two-part; minute width min(width, 2).
		{"125:30", 7530 - 7000, false, "08:50", false},
		{"60:00", 7200, false, "120:00", false},
		{"60:00", 1000 * 60, false, "", false},
		{"60:00", 999*60 + 59, false, "999:59", false},
		// Fractions: half up at the written precision, the separator kept.
		{"0:00.5", 2.25, false, "0:02.3", false},
		{"0:00.5", 2.35, false, "0:02.4", false},    // 2.35 * 10 + 0.5 = 24 (floor)
		{"0:00,25", 1.005, false, "0:01,00", false}, // 1.005 * 100 is 100.99999999999999
		{"0:00.000", 0.0005, false, "0:00.001", false},
		{"0:00.000", 59.9996, false, "1:00.000", false},
		{"0:00", 0.4999999, false, "0:00", false},
		{"0:00", 0.5, false, "0:01", false},
		{"0:00", 0.49999999999999994, false, "0:01", false}, // floor(x + 0.5) as in JS; math.Round gives 0
		// The 100-hour ceiling (R13.9.2).
		{"1:00:00", 360000, false, "", false},
		{"1:00:00", 359999, false, "99:59:59", false},
		{"00:00", 359999.4, false, "99:59:59", true},
		{"00:00", 9007199254740992, false, "", false},
		{"00:00", math.Inf(1), false, "", false},
		{"00:00", math.NaN(), false, "", false},
	}
	for _, c := range cases {
		f, ok := formatTime(c.seconds, styleOf(t, c.style), c.oneDigit)
		got := ""
		if ok {
			got = f.raw
		}
		if got != c.want || (ok && f.upgraded != c.upgraded) {
			t.Errorf("formatTime(%v in %s, %v) = %q %v, want %q %v", c.seconds, c.style, c.oneDigit,
				got, f.upgraded, c.want, c.upgraded)
		}
	}
}

func TestWithoutMarkers(t *testing.T) {
	cases := []struct{ in, want string }{
		{"*Noisia* - Stigma", "Noisia - Stigma"},
		{"**Noisia** - __Stigma__", "Noisia - Stigma"},
		{"_a_ *b* **c** __d__", "a b c d"},
		{"***x***", "***x***"},
		{"a*b*c", "a*b*c"},
		{"2 * 3 * 4", "2 * 3 * 4"},
		{"*a **b* c**", "a **b c**"},
		{"(*a*)", "(a)"},
		{"*unclosed", "*unclosed"},
		{"snake_case_name", "snake_case_name"},
	}
	for _, c := range cases {
		if got := withoutMarkers([]rune(c.in), 0, noRune); got != c.want {
			t.Errorf("withoutMarkers(%q) = %q, want %q", c.in, got, c.want)
		}
	}
	if emphasisRun([]rune("***"), 0) != 0 || emphasisRun([]rune("__x"), 0) != 2 || emphasisRun([]rune("x"), 0) != 0 {
		t.Error("emphasisRun")
	}
	// Out-of-range starts are total.
	if withoutMarkers([]rune("ab"), 5, '*') != "" || withoutMarkers(nil, 0, '*') != "" {
		t.Error("withoutMarkers bounds")
	}
}

func TestKindsHelpers(t *testing.T) {
	if got := fmt.Sprint(markerWords(" (Intro)/shout-out [x]{ } ")); got != "[Intro shout out x]" {
		t.Errorf("markerWords = %s", got)
	}
	if layeredPrefix([]rune("w/ Mefjus - Sinkhole")) != 3 || layeredPrefix([]rune("W/  x")) != 4 ||
		layeredPrefix([]rune("w/")) != 0 || layeredPrefix([]rune("w/  ")) != 0 ||
		layeredPrefix([]rune("with x")) != 0 {
		t.Error("layeredPrefix")
	}
	if !isBlank("") || !isBlank(" \u3000\t") || isBlank(" x") {
		t.Error("isBlank")
	}
	if !isLink("  HTTPS://x.y") || !isLink("www.example.com") || isLink("wwwx") || isLink("http:/x") {
		t.Error("isLink")
	}
	if !isHeader("Tracklist:") || !isHeader(" tracklist ") || !isHeader("Tracklist 2024") ||
		isHeader("Tracklist of the year") || isHeader("Noisia") {
		t.Error("isHeader")
	}
	m := markerTableFor(nil)
	if !hasMarkerWord("Intro", m) || hasMarkerWord("Noisia - Stigma", m) {
		t.Error("hasMarkerWord default")
	}
	extra := markerTableFor([]string{"Zebra Crossing"})
	if !hasMarkerWord("the zebra-crossing", extra) || hasMarkerWord("zebra", extra) {
		t.Error("hasMarkerWord extra")
	}
	tr := Track{Artists: []Artist{{Name: "A", Role: RolePrimary}}}
	if !hasSeparator(&tr, false) || hasSeparator(&tr, true) {
		t.Error("hasSeparator")
	}
	tr.Warnings = []Warning{WarningNoSeparator}
	if hasSeparator(&tr, false) {
		t.Error("hasSeparator noSeparator")
	}
	if !hasSeparator(&Track{Flags: Flags{UnknownArtist: true}}, false) || hasSeparator(&Track{}, false) {
		t.Error("hasSeparator unknownArtist")
	}
}

func TestBarePositions(t *testing.T) {
	b := findBareCandidate("12 Noisia - Stigma", false)
	if b == nil || b.raw != "12" || b.number != 12 || b.stripped != "Noisia - Stigma" {
		t.Errorf("findBareCandidate = %+v", b)
	}
	for _, in := range []string{"1234 x", "12\u00a0x", "12 ", "12  \u3000", "x 12", "12"} {
		if findBareCandidate(in, false) != nil {
			t.Errorf("findBareCandidate(%q) should be nil", in)
		}
	}
	if findBareCandidate("1 x", true) != nil {
		t.Error("a taken position is not a bare candidate")
	}
	got := confirmPositions(
		[]int{1, -1, 2, 7, 5, -1, 4},
		[]bool{true, false, true, true, true, true, true},
	)
	// 1-2 are neighbours; 7 and 5 have no adjacent neighbour; 4 follows a candidate line
	// without a number and so is not confirmed by 5.
	if fmt.Sprint(got) != "[true false true false false false false]" {
		t.Errorf("confirmPositions = %v", got)
	}
	// A non-candidate line between two numbers does not separate them.
	if fmt.Sprint(confirmPositions([]int{3, -1, 4}, []bool{true, false, true})) != "[true false true]" {
		t.Error("confirmPositions skips non-candidates")
	}
	if len(confirmPositions(nil, nil)) != 0 {
		t.Error("confirmPositions empty")
	}
}

func TestSearchTime(t *testing.T) {
	f := func(v float64) *float64 { return &v }
	cases := []struct {
		raw    string
		lo, hi *float64
		want   string // "none", "many" or the raw of the one candidate
	}{
		// The corpus case: the second step finds it (see TestSuggestions).
		{"1:22:21", f(3480), f(3780), "1:02:21"},
		// 3741 is not below hi: two-digit changes, several of them.
		{"1:22:21", f(3480), f(3741), "many"},
		// One digit finds nothing, so two count: 02:00 is the only one.
		{"10:00", f(110), f(130), "02:00"},
		// Bounds are strict.
		{"10:00", f(120), f(130), "none"},
		{"01:00", f(120), f(300), "many"},
		{"10:00", f(600), f(601), "none"},
		{"10:00", nil, nil, "many"},
		{"0:00.5", f(59), f(61), "1:00.5"},
		// Fraction digits never change.
		{"0:00.5", f(0.4), f(0.45), "none"},
	}
	for _, c := range cases {
		outcome, tm := searchTime(c.raw, c.lo, c.hi)
		got := map[searchOutcome]string{searchNone: "none", searchMany: "many", searchOne: tm.Raw}[outcome]
		if got != c.want {
			t.Errorf("searchTime(%q) = %q, want %q", c.raw, got, c.want)
		}
	}
}

// entriesOf builds track entries from lines without the pipeline: every line with text is a
// track.
func entriesOf(text string) []Entry {
	lines := splitLines(text)
	out := make([]Entry, len(lines))
	for i, l := range lines {
		s := scanTime(l.raw)
		kind := EntryKindTrack
		if isBlank(l.raw) {
			kind = EntryKindBlank
		}
		out[i] = Entry{Line: i + 1, Kind: kind, Eol: l.eol, Raw: l.raw, Parts: s.parts, Time: s.time}
	}
	return out
}

func warningsText(ws []TracklistWarning) string {
	var b strings.Builder
	for _, w := range ws {
		fmt.Fprintf(&b, "%d %s %s", w.Line, w.Code, w.Severity)
		if w.Suggestion != nil {
			fmt.Fprintf(&b, " -> %d %s %v", w.Suggestion.Line, w.Suggestion.Raw, w.Suggestion.Seconds)
		}
		b.WriteString("\n")
	}
	return b.String()
}

func TestSuggestions(t *testing.T) {
	// The JS reference's warnings for this text (rust/tests/tracklist_api.rs).
	text := "0:10 A - B\n58:00 K - L\n1:22:21 C - D\n1:03:00 E - F\n1:04:00 G - H\nI - J\n1:75 x - y"
	entries := entriesOf(text)
	invalid := make([]bool, len(entries))
	for i, e := range entries {
		invalid[i] = scanTime(e.Raw).invalid
	}
	got := warningsText(tracklistWarnings(&warningInput{entries: entries, invalid: invalid}))
	want := "1 no-start-at-zero info\n" +
		"4 time-out-of-order warning -> 3 1:02:21 3741\n" +
		"6 time-missing warning\n" +
		"7 time-missing warning\n" +
		"7 invalid-time warning\n"
	if got != want {
		t.Errorf("warnings:\n%s\nwant:\n%s", got, want)
	}
}

func TestWarnings(t *testing.T) {
	entries := entriesOf("00:00 a\n00:00 b\n02:00 c\n01:00 d\n\nw/ e\n05:00 f")
	entries[5].Layered = true // R13.13.3: no time-missing
	audio := 200.0
	got := warningsText(tracklistWarnings(&warningInput{
		entries:      entries,
		positions:    []int{1, 1, 3, -1, -1, -1, 4},
		unparsed:     []bool{false, false, false, false, false, false, true},
		audioSeconds: &audio,
	}))
	want := "2 time-duplicate warning\n" +
		"2 position-duplicate warning\n" +
		"3 position-gap warning\n" +
		"4 time-out-of-order warning\n" + // 03:00 and 04:00 both fit
		"7 time-after-audio-end warning\n" +
		"7 line-unparsed warning\n"
	if got != want {
		t.Errorf("warnings:\n%s\nwant:\n%s", got, want)
	}
	// No timed line: no time-missing.
	if ws := tracklistWarnings(&warningInput{entries: entriesOf("a - b\nc - d")}); len(ws) != 0 {
		t.Errorf("untimed list warns %v", ws)
	}
}

func TestStats(t *testing.T) {
	s := statsOf(entriesOf("x\n00:30 a\n\n01:30 b"))
	if s.Lines != 4 || s.Tracks != 3 || s.Timed != 2 || *s.FirstTime != 30 || *s.LastTime != 90 {
		t.Errorf("stats %+v", s)
	}
	if s := statsOf(nil); s.FirstTime != nil || s.LastTime != nil || s.Lines != 0 {
		t.Errorf("empty stats %+v", s)
	}
}

func wireEdits(t *testing.T, text, edits string) string {
	t.Helper()
	v, err := parseJSON([]byte(edits))
	if err != nil {
		t.Fatal(err)
	}
	return string(wireCall("applyEdits", text, jsonValue{kind: jsonObject}, jsonValue{kind: jsonObject}, v))
}

func TestEditResults(t *testing.T) {
	text := "00:00 Noisia - Stigma\n59:30 Phace - Psycho\n"
	r := ApplyEdits(text, []Edit{ShiftFrom(45, 2), SetTime(1, -3), Delete(7)})
	got := string(marshal(&r))
	want := `{"text":"00:00 Noisia - Stigma\n01:00:15 Phace - Psycho\n","changed":[1,2],"warnings":[{"code":"style-upgraded","edit":0,"line":2},{"code":"time-clamped","edit":1,"line":1},{"code":"edit-invalid","edit":2,"line":7}]}`
	if got != want {
		t.Errorf("got  %s\nwant %s", got, want)
	}
	if r := ApplyEdits("0:00.5 A - B", []Edit{SetTime(1, 2.25)}); r.Text != "0:02.3 A - B" {
		t.Errorf("half-up rounding: %q", r.Text)
	}
	tooLong := ApplyEdits("1:00:00 A - B", []Edit{SetTime(1, 360000)})
	if tooLong.Text != "1:00:00 A - B" || tooLong.Warnings[0].Code != EditWarningCodeEditInvalid {
		t.Errorf("100 hours: %+v", tooLong)
	}
	// One-digit hours when every time with hours has them (R13.9.2).
	if r := ApplyEdits("0:00 a\n1:00:00 b\n59:00 c", []Edit{Shift(120)}); r.Text != "2:00 a\n1:02:00 b\n1:01:00 c" {
		t.Errorf("one-digit hours: %q", r.Text)
	}
	if r := ApplyEditsWith("00:00 A - B\n01:00 C - D", []Edit{Shift(5)}, TracklistOptions{}); r.Text != "00:05 A - B\n01:05 C - D" {
		t.Errorf("ApplyEditsWith: %q", r.Text)
	}
	if r := ApplyEdits("x", nil); r.Text != "x" || r.Changed == nil || len(r.Warnings) != 0 {
		t.Errorf("no edits: %+v", r)
	}
}

func TestEditOps(t *testing.T) {
	cases := []struct{ text, edits, want string }{
		// sort-by-time: blocks (a timed line and the untimed lines after it), stable on ties;
		// leading untimed lines stay; eols stay in their slots.
		{"head\n03:00 c\nnote c\n01:00 a\n03:00 c2\n01:00 a2\r\n", `[{"op":"sort-by-time"}]`,
			`{"text":"head\n01:00 a\n01:00 a2\n03:00 c\nnote c\n03:00 c2\r\n","changed":[2,3,4,5,6],"warnings":[]}`},
		{"a\nb", `[{"op":"sort-by-time"}]`, `{"text":"a\nb","changed":[],"warnings":[]}`},
		// set-rest adds a space after a gap without one.
		{"[00:00]A", `[{"op":"set-rest","line":1,"rest":"X - Y"}]`,
			`{"text":"[00:00] X - Y","changed":[1],"warnings":[]}`},
		{"00:00 A", `[{"op":"set-rest","line":1,"rest":""}]`,
			`{"text":"00:00 ","changed":[1],"warnings":[]}`},
		{"untimed", `[{"op":"set-rest","line":1,"rest":"z"}]`, `{"text":"z","changed":[1],"warnings":[]}`},
		// insert: the most common eol, the end, an empty text.
		{"a\r\nb\r\nc\n", `[{"op":"insert","after":3,"text":"d"}]`,
			`{"text":"a\r\nb\r\nc\nd\r\n","changed":[4],"warnings":[]}`},
		{"a\nb", `[{"op":"insert","after":2,"text":"c"}]`,
			`{"text":"a\nb\nc","changed":[2,3],"warnings":[]}`},
		{"", `[{"op":"insert","after":0,"text":"x"}]`, `{"text":"x","changed":[1],"warnings":[]}`},
		{"a\rb\n", `[{"op":"insert","after":0,"text":"z"}]`,
			`{"text":"z\ra\rb\n","changed":[1],"warnings":[]}`},
		// delete: the deleted last line's eol moves up; a CR line and an empty LF line merge.
		{"a\r\nb", `[{"op":"delete","line":2}]`, `{"text":"a","changed":[1],"warnings":[]}`},
		{"a\rx\n\n", `[{"op":"delete","line":2}]`, `{"text":"a\r\n","changed":[1],"warnings":[]}`},
		{"only", `[{"op":"delete","line":1}]`, `{"text":"","changed":[],"warnings":[]}`},
		// An invalid edit changes nothing; the others still apply.
		{"00:00 a\n01:00 b", `[{"op":"shift","seconds":-30,"lines":[2]},{"op":"set-time","line":3,"seconds":1},{"op":"shift","seconds":-90}]`,
			`{"text":"00:00 a\n00:00 b","changed":[1,2],"warnings":[{"code":"edit-invalid","edit":1,"line":3},{"code":"time-clamped","edit":2,"line":1},{"code":"time-clamped","edit":2,"line":2}]}`},
		// A shift that would push one line past 100 hours fails as a whole.
		{"00:00 a\n99:00:00 b", `[{"op":"shift","seconds":7200}]`,
			`{"text":"00:00 a\n99:00:00 b","changed":[],"warnings":[{"code":"edit-invalid","edit":0,"line":null}]}`},
	}
	for _, c := range cases {
		if got := wireEdits(t, c.text, c.edits); got != c.want {
			t.Errorf("%q %s\n got  %s\n want %s", c.text, c.edits, got, c.want)
		}
	}
}

func TestLooseEditsFollowJSNumberRules(t *testing.T) {
	text := "00:00 A - B\n01:00 C - D"
	invalid := map[string]string{
		`[{"op":"set-time","line":true,"seconds":5}]`:           "null",
		`[{"op":"set-time","line":1,"seconds":true}]`:           "1",
		`[{"op":"set-time","line":1,"seconds":null}]`:           "1",
		`[{"op":"set-time","line":1,"seconds":"5"}]`:            "1",
		`[{"op":"set-time","line":1,"seconds":1e999}]`:          "1",
		`[{"op":"set-time","line":1.5,"seconds":5}]`:            "null",
		`[{"op":"shift","seconds":-1e999}]`:                     "null",
		`[{"op":"shift","seconds":1,"lines":null}]`:             "null",
		`[{"op":"shift","seconds":1,"lines":[1,3]}]`:            "null",
		`[{"op":"shift","seconds":1,"from":0}]`:                 "null",
		`[{"op":"shift","seconds":9007199254740992}]`:           "null",
		`[{"op":"rename","line":-2}]`:                           "-2",
		`[{"op":"rename","line":"x","after":1e300}]`:            "1e+300",
		`[{"op":"insert","after":3,"text":"x"}]`:                "3",
		`[{"op":"insert","after":1,"text":"a\rb"}]`:             "1",
		`[{"op":"insert","after":1,"text":5}]`:                  "1",
		`[{"op":"set-rest","line":1,"rest":"a\nb"}]`:            "1",
		`[{"op":"delete"}]`:                                     "null",
		`[{"op":"delete","line":2.0000001}]`:                    "null",
		`[null]`:                                                "null",
		`[[]]`:                                                  "null",
		`[{"line":1}]`:                                          "1",
		`[{"op":"sort-by-time ","line":1}]`:                     "1",
		`[{"op":"set-time","line":-0,"seconds":1,"after":2}]`:   "0",
		`[{"op":"set-time","line":1e21,"seconds":1,"after":2}]`: "1e+21",
	}
	for edits, line := range invalid {
		out, err := parseJSON([]byte(wireEdits(t, text, edits)))
		if err != nil {
			t.Fatal(err)
		}
		gotText, _ := out.getString("text")
		ws, _ := out.get("warnings")
		if gotText != text || len(ws.arr) != 1 {
			t.Errorf("%s: %s", edits, marshal(out))
			continue
		}
		code, _ := ws.arr[0].getString("code")
		l, _ := ws.arr[0].get("line")
		if code != "edit-invalid" || string(marshal(l)) != line {
			t.Errorf("%s: warning %s, want edit-invalid with line %s", edits, marshal(ws.arr[0]), line)
		}
	}
	// lines wins over from; a value that is not an array of edits has nothing to do.
	if got := wireEdits(t, text, `[{"op":"shift","seconds":1,"lines":[2],"from":"bad"}]`); got != `{"text":"00:00 A - B\n01:01 C - D","changed":[2],"warnings":[]}` {
		t.Errorf("lines over from: %s", got)
	}
	if wireEdits(t, text, `{"op":"sort-by-time"}`) != wireEdits(t, text, `[]`) {
		t.Error("an object is not a list of edits")
	}
	// Duplicate and unordered lines shift once each.
	if got := wireEdits(t, text, `[{"op":"shift","seconds":2,"lines":[2,1,2]}]`); got != `{"text":"00:02 A - B\n01:02 C - D","changed":[1,2],"warnings":[]}` {
		t.Errorf("lines set: %s", got)
	}
}

func TestTypedEditsMatchTheWire(t *testing.T) {
	texts := []string{
		"",
		"\n",
		"00:00 Noisia - Stigma\n03:30 Phace - Psycho\nrecorded live\n07:15 Mefjus - Sinkhole",
		"Tracklist:\r\n[00:00] Intro\r\n[01:30] 1 Noisia - Stigma\r\n[05:00] 2 Phace - Psycho\r\n",
		"**00:00** *Noisia* - Stigma\r- 03:30 Camo & Krooked - Climax\rw/ Mefjus - Sinkhole\r",
		"0:00 A - B\n1:22:21 C - D\n1:03:00 E - F\n1:75 bad\nhttps://example.com\n",
		"01:02:03.250 🎵 - 𝐀𝐁 (👨‍👩‍👧 Remix)\u2028still the same line\n\u3000\n12:34,5 x",
	}
	cases := []struct {
		typed []Edit
		loose string
	}{
		{[]Edit{SetTime(2, 3725)}, `[{"op":"set-time","line":2,"seconds":3725}]`},
		{[]Edit{SetTime(2, -4.5)}, `[{"op":"set-time","line":2,"seconds":-4.5}]`},
		{[]Edit{SetTime(9, 1)}, `[{"op":"set-time","line":9,"seconds":1}]`},
		{[]Edit{SetTime(1, math.Inf(1))}, `[{"op":"set-time","line":1,"seconds":1e999}]`},
		{[]Edit{SetTime(1, math.NaN())}, `[{"op":"set-time","line":1,"seconds":null}]`},
		{[]Edit{Shift(30)}, `[{"op":"shift","seconds":30}]`},
		{[]Edit{ShiftFrom(-3600, 2)}, `[{"op":"shift","seconds":-3600,"from":2}]`},
		{[]Edit{{Op: EditOpShift, Seconds: 0.25, Lines: []int{4, 1, 4}, From: ptrInt(3)}},
			`[{"op":"shift","seconds":0.25,"lines":[4,1,4],"from":3}]`},
		{[]Edit{ShiftLines(1, 0)}, `[{"op":"shift","seconds":1,"lines":[0]}]`},
		{[]Edit{ShiftLines(1)}, `[{"op":"shift","seconds":1,"lines":[]}]`},
		{[]Edit{SortByTime()}, `[{"op":"sort-by-time"}]`},
		{[]Edit{SetRest(1, "New - Rest")}, `[{"op":"set-rest","line":1,"rest":"New - Rest"}]`},
		{[]Edit{Insert(0, "00:00 X - Y")}, `[{"op":"insert","after":0,"text":"00:00 X - Y"}]`},
		{[]Edit{Insert(99, "x")}, `[{"op":"insert","after":99,"text":"x"}]`},
		{[]Edit{Delete(1)}, `[{"op":"delete","line":1}]`},
		{[]Edit{Delete(1), Insert(1, "9:59:59 Late - One"), SortByTime(), Shift(5000)},
			`[{"op":"delete","line":1},{"op":"insert","after":1,"text":"9:59:59 Late - One"},{"op":"sort-by-time"},{"op":"shift","seconds":5000}]`},
	}
	for _, c := range cases {
		for _, text := range texts {
			r := ApplyEdits(text, c.typed)
			if got, want := string(marshal(&r)), wireEdits(t, text, c.loose); got != want {
				t.Errorf("%s on %q:\n typed %s\n wire  %s", c.loose, text, got, want)
			}
		}
	}
}

func ptrInt(n int) *int { return &n }

// TestTracklistTotality feeds random text and random edits through the tracklist code: nothing
// panics, entries are lossless, and edits keep lines no edit wrote.
func TestTracklistTotality(t *testing.T) {
	alphabet := []rune("0123456789:.,[]()*_-–—•|=> \t\r\n\u2028\u0085\u3000aZé🎵w/")
	rng := rand.New(rand.NewSource(7))
	for k := 0; k < 3000; k++ {
		n := rng.Intn(60)
		rs := make([]rune, n)
		for i := range rs {
			rs[i] = alphabet[rng.Intn(len(alphabet))]
		}
		text := string(rs)
		l := ParseTracklist(text)
		var b strings.Builder
		for _, e := range l.Entries {
			p := e.Parts
			if p.Lead+p.Time+p.Gap+p.Rest != e.Raw {
				t.Fatalf("%q: parts of line %d are not lossless", text, e.Line)
			}
			b.WriteString(e.Raw + string(e.Eol))
		}
		if b.String() != text {
			t.Fatalf("%q: entries join to %q", text, b.String())
		}
		edits := []Edit{
			SetTime(rng.Intn(5), float64(rng.Intn(400000))-1000),
			ShiftFrom(float64(rng.Intn(8000))-4000, rng.Intn(5)),
			SortByTime(),
			Insert(rng.Intn(5), "12:00 x"),
			Delete(rng.Intn(5)),
			SetRest(rng.Intn(5), "**y**"),
		}
		rng.Shuffle(len(edits), func(i, j int) { edits[i], edits[j] = edits[j], edits[i] })
		r := ApplyEdits(text, edits[:1+rng.Intn(len(edits))])
		for i := 1; i < len(r.Changed); i++ {
			if r.Changed[i] <= r.Changed[i-1] {
				t.Fatalf("%q: changed not sorted: %v", text, r.Changed)
			}
		}
	}
}

func TestWarningRankFollowsAllWarningCodes(t *testing.T) {
	codes := AllWarningCodes()
	for i, c := range codes {
		if warningRank(c) != i {
			t.Errorf("warningRank(%s) = %d, want %d", c, warningRank(c), i)
		}
	}
	if warningRank("bogus") != len(codes) {
		t.Error("an unknown code ranks last")
	}
	// The lists are fresh slices: changing one changes neither the next call nor the ranking.
	codes[0], codes[1] = codes[1], codes[0]
	if AllWarningCodes()[0] != WarningCodeTimeMissing || warningRank(WarningCodeTimeMissing) != 0 {
		t.Error("AllWarningCodes shares its backing array")
	}
}
