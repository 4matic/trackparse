package trackparse

import (
	"strings"
	"testing"
	"unicode/utf8"
)

// Unit tests for the single-track pipeline helpers: R1 normalize, R2 mode, R3 scanner, R4
// prefixes, R6 separator, R8 title, and a fuzz test that the engine never panics (R0.8).

func TestNormalize(t *testing.T) {
	cases := []struct{ in, want string }{
		{"", ""},
		{"  a \t b  ", "a b"},
		{"Kid Cudi \u2013 Pursuit", "Kid Cudi - Pursuit"},                // R1.5 en dash
		{"a \u2014 b", "a - b"},                                          // R1.5 em dash
		{"a -- b", "a - b"},                                              // R1.5 spaced run
		{"a--b", "a--b"},                                                 // unspaced run kept
		{"a --b", "a --b"},                                               // one side only
		{"\u3010Official\u3011\uff08x\uff09", "[Official](x)"},           // R1.3
		{"\u201cq\u201d \u2018s\u2019", "\"q\" 's'"},                     // R1.4
		{"a\u200bb\ufeffc\u00add\u2060e", "abcde"},                       // R1.2
		{"a\u200db", "a\u200db"},                                         // ZWJ kept
		{"a\ue000b", "ab"},                                               // the placeholder never survives
		{"e\u0301", "\u00e9"},                                            // NFC
		{"e\u200b\u0301", "\u00e9"},                                      // R1.8 compose again
		{"a\u00a0\u3000b\u2028c", "a b c"},                               // R0.2 whitespace
		{"x\u2212y", "x-y"},                                              // minus sign
		{"\u2014\u2014", "--"},                                           // no spaces around: kept
		{"a \u2013\u2014 b", "a - b"},                                    // mixed spaced run
		{"\u00ab\u00bb\u300c\u300d\u300e\u300f\uff02", "\"\"\"\"\"\"\""}, // quotes
	}
	for _, c := range cases {
		if got := normalize(c.in); got != c.want {
			t.Errorf("normalize(%q) = %q, want %q", c.in, got, c.want)
		}
		if got := normalize(normalize(c.in)); got != normalize(c.in) {
			t.Errorf("normalize is not idempotent on %q", c.in)
		}
	}
}

func TestScan(t *testing.T) {
	sk, unbalanced := scan([]rune("A (b [c]) d [e"), 10)
	if !unbalanced {
		t.Fatal("expected unbalanced")
	}
	if string(sk.text) != "A "+string(ph)+" d "+string(ph) {
		t.Fatalf("skeleton %q", string(sk.text))
	}
	gs := groupsOf(sk)
	if len(gs) != 2 {
		t.Fatalf("groups %d", len(gs))
	}
	if g := gs[0].g; g.inner != "b [c]" || g.raw != "(b [c])" || g.pos != 12 || g.open != DelimiterParen {
		t.Errorf("first group %+v", *g)
	}
	if g := gs[1].g; g.inner != "e" || g.raw != "[e" || g.pos != 22 || g.open != DelimiterSquare {
		t.Errorf("second group %+v", *g)
	}
	if got := renderSkel(sk); got != "A (b [c]) d [e" {
		t.Errorf("render %q", got)
	}
	// R3.2: a non-matching closer is literal text inside the group.
	sk, unbalanced = scan([]rune("x (a]b) y"), 0)
	if unbalanced || len(groupsOf(sk)) != 1 || groupsOf(sk)[0].g.inner != "a]b" {
		t.Errorf("non-matching closer: %q %v", string(sk.text), unbalanced)
	}
	// Offsets are UTF-16 units.
	sk, _ = scan([]rune("\U0001f600 (x)"), 0)
	if p := sk.pos[len(sk.pos)-1]; p != 3 {
		t.Errorf("astral offset %d, want 3", p)
	}
	// A stray closer outside groups is text.
	sk, unbalanced = scan([]rune("a) b"), 0)
	if unbalanced || string(sk.text) != "a) b" {
		t.Errorf("stray closer %q", string(sk.text))
	}
}

func TestSkelOps(t *testing.T) {
	sk, _ := scan([]rune("  a (x) b  "), 0)
	tr := trimSkel(sk)
	if renderSkel(tr) != "a (x) b" || startPos(tr, -1) != 2 {
		t.Errorf("trim %q %d", renderSkel(tr), startPos(tr, -1))
	}
	if s := sliceSkel(sk, 50, 60); !s.isEmpty() {
		t.Error("out-of-range slice should be empty")
	}
	if s := sliceSkel(sk, 5, 2); !s.isEmpty() {
		t.Error("reversed slice should be empty")
	}
	if startPos(sliceSkel(sk, 0, 0), 7) != 7 {
		t.Error("startPos fallback")
	}
	g := groupsOf(tr)[0].g
	without := removeGroups(tr, map[int]bool{g.pos: true})
	if renderSkel(without) != "a  b" {
		t.Errorf("removeGroups %q", renderSkel(without))
	}
	cat := concatSkel(sliceSkel(tr, 0, 1), sliceSkel(tr, 2, 5))
	if renderSkel(cat) != "a(x) b" {
		t.Errorf("concat %q", renderSkel(cat))
	}
}

func segTexts(segs []segment) []string {
	out := make([]string, len(segs))
	for i, s := range segs {
		out[i] = renderSkel(s.skel)
	}
	return out
}

func TestSplitAtSpacedDashes(t *testing.T) {
	cases := []struct {
		in      string
		leading bool
		want    string
	}{
		{"A - B - C", true, "A|B|C"},
		{"A-B", true, "A-B"},
		{"- B", true, "|B"},
		{"- B", false, "- B"},
		{"A -B", true, "A -B"},
		{"A - ", true, "A|"},
		{"", true, ""},
		{"- - x", true, "||x"},
	}
	for _, c := range cases {
		sk, _ := scan([]rune(c.in), 0)
		got := strings.Join(segTexts(splitAtSpacedDashes(sk, c.leading)), "|")
		if got != c.want {
			t.Errorf("split(%q, %v) = %q, want %q", c.in, c.leading, got, c.want)
		}
	}
}

func TestSplitPipes(t *testing.T) {
	cases := []struct{ in, want string }{
		{"A | B || C", "A|B|C"},
		{"A|B", "A|B"},
		{"A | (x | y) | B", "A|(x | y)|B"},
		{"A ||| B", "A ||| B"},
		{"A | ", "A|"},
	}
	for _, c := range cases {
		sk, _ := scan([]rune(c.in), 0)
		got := strings.Join(segTexts(splitPipes(sk)), "|")
		if c.in == "A|B" {
			got = renderSkel(sk) // one segment
		}
		if got != c.want {
			t.Errorf("splitPipes(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestMatchTimestamp(t *testing.T) {
	cases := []struct {
		in      string
		ok      bool
		raw     string
		seconds int
		rest    string
	}{
		{"1:02:03 Artist", true, "1:02:03", 3723, "Artist"},
		{"[05:30] - Artist", true, "05:30", 330, "Artist"},
		{"(4:59) X", true, "4:59", 299, "X"},
		{"60:00 X", false, "", 0, ""},
		{"5:60 X", false, "", 0, ""},
		{"12:34X", false, "", 0, ""},
		{"[12:34 X", false, "", 0, ""},
		{"123:45 X", false, "", 0, ""},
		{"1:02:60 X", false, "", 0, ""}, // "1:02" is followed by ":", not a space
		{"12:34", true, "12:34", 754, ""},
	}
	for _, c := range cases {
		s := []rune(c.in)
		ts, end, ok := matchTimestamp(s)
		if ok != c.ok {
			t.Errorf("matchTimestamp(%q) ok = %v", c.in, ok)
			continue
		}
		if ok && (ts.Raw != c.raw || ts.Seconds != c.seconds || string(tail(s, end)) != c.rest) {
			t.Errorf("matchTimestamp(%q) = %+v rest %q", c.in, ts, string(tail(s, end)))
		}
	}
}

func TestMatchPosition(t *testing.T) {
	cases := []struct {
		in   string
		mode Mode
		end  int // -1: no position
	}{
		{"01. Artist", ModeClean, 4},
		{"1) Artist", ModeClean, 3},
		{"01 Artist", ModeClean, 3},
		{"1 Artist", ModeClean, -1},
		{"2 Unlimited - No Limit", ModeClean, -1},
		{"3 - Artist - Title", ModeClean, 4},
		{"3 - Title", ModeClean, -1},
		{"311 - Amber", ModeFilename, 6},
		{"01-Track", ModeFilename, 3},
		{"01-Track", ModeClean, -1},
		{"03_name", ModeFilename, 3},
		{"3 My Song", ModeFilename, 2},
		{"3 My - Song", ModeFilename, -1},
		{"1234. X", ModeClean, -1},
		{"01. - X", ModeClean, 6},
		{"1.", ModeClean, 2},
	}
	for _, c := range cases {
		end, ok := matchPosition([]rune(c.in), c.mode)
		if !ok {
			end = -1
		}
		if end != c.end {
			t.Errorf("matchPosition(%q, %s) = %d, want %d", c.in, c.mode, end, c.end)
		}
	}
}

func TestStripPrefixes(t *testing.T) {
	e := newEngine(resolvedOptions{knownArtists: []string{"3 Doors Down"}})
	c := e.newCtx()
	r := stripPrefixes([]rune("[01:02] 03. A - B"), 5, ModeClean, c)
	if r.timestamp == nil || r.timestamp.Seconds != 62 || r.position == nil ||
		r.position.Raw != "03" || r.position.Number != 3 || string(r.rest) != "A - B" ||
		r.base != 5+12 {
		t.Errorf("stripPrefixes %+v", r)
	}
	// A known artist is never a position (R7.3a).
	r = stripPrefixes([]rune("3 Doors Down-Kryptonite"), 0, ModeFilename, c)
	if r.position != nil {
		t.Errorf("known artist taken as position: %+v", r.position)
	}
	// A position never takes the whole string.
	r = stripPrefixes([]rune("01."), 0, ModeClean, c)
	if r.position != nil || string(r.rest) != "01." {
		t.Errorf("whole-string position: %+v", r)
	}
}

func TestSeparatorHelpers(t *testing.T) {
	if i, ok := asymmetricDash([]rune("A -B")); !ok || i != 2 {
		t.Errorf("asymmetricDash = %d %v", i, ok)
	}
	if _, ok := asymmetricDash([]rune("A - B")); ok {
		t.Error("spaced dash is not asymmetric")
	}
	if i, ok := tightUpperDash([]rune("Jay-Z-Numb")); !ok || i != 5 {
		t.Errorf("tightUpperDash = %d %v", i, ok)
	}
	if _, ok := tightUpperDash([]rune("well-known")); ok {
		t.Error("lowercase right side")
	}
	if i, ok := underscoreDash([]rune("A_-_B")); !ok || i != 1 {
		t.Errorf("underscoreDash = %d %v", i, ok)
	}

	c := sharedEngine().newCtx()
	sk, _ := scan([]rune("Jay-Z-Numb"), 0)
	if i, ok := unspacedDash(sk, c); !ok || i != 5 {
		t.Errorf("unspacedDash = %d %v", i, ok)
	}
	sk, _ = scan([]rune("Tentacles by Noisia (Official Video)"), 0)
	if i, ok := bySplit(sk, c); !ok || i != 9 {
		t.Errorf("bySplit = %d %v", i, ok)
	}
	sk, _ = scan([]rune("Stand by Me"), 0)
	if _, ok := bySplit(sk, c); ok {
		t.Error("bySplit on a single stopword")
	}
	sk, _ = scan([]rune("Song by (Official Video)"), 0)
	if _, ok := bySplit(sk, c); ok {
		t.Error("bySplit with only a group on the right")
	}
	sk, _ = scan([]rune(`Taylor Swift: "Shake It Off" (Official Video)`), 0)
	parts, ok := quotedTitle(sk)
	if !ok || parts.artist == nil || renderSkel(*parts.artist) != "Taylor Swift" ||
		renderSkel(parts.title) != "Shake It Off (Official Video)" {
		t.Errorf("quotedTitle %v", ok)
	}
	sk, _ = scan([]rune(`"Shake It Off"`), 0)
	if _, ok := quotedTitle(sk); ok {
		t.Error("quote at index 0")
	}
}

func TestFindSeparator(t *testing.T) {
	run := func(in string, mode Mode, list bool) (string, string, []Warning) {
		c := sharedEngine().newCtx()
		sk, _ := scan([]rune(in), 0)
		r := findSeparator(separatorInput{skel: sk, mode: mode, listContext: list}, c)
		artist := "<nil>"
		if r.artist != nil {
			artist = renderSkel(*r.artist)
		}
		return artist, renderSkel(r.title), c.warnings
	}
	cases := []struct {
		in        string
		mode      Mode
		list      bool
		artist    string
		title     string
		warnCount int
	}{
		{"A - B - C", ModeClean, false, "A", "B - C", 0},
		{"- B", ModeClean, false, "<nil>", "B", 0},
		{"Title", ModeClean, false, "<nil>", "Title", 1},
		{"A -B", ModeClean, false, "A", "B", 1},
		{"Jay-Z-Numb", ModeYouTube, false, "Jay-Z", "Numb", 1},
		{"Jay-Z-Numb", ModeClean, false, "<nil>", "Jay-Z-Numb", 1},
		{"Jay-Z-Numb", ModeTracklist, true, "Jay-Z", "Numb", 1},
		{"Jay-Z-Numb", ModeTracklist, false, "<nil>", "Jay-Z-Numb", 1},
		{"A_-_B", ModeTracklist, false, "A", "B", 1},
		{"", ModeClean, false, "<nil>", "", 0},
	}
	for _, c := range cases {
		a, ti, w := run(c.in, c.mode, c.list)
		if a != c.artist || ti != c.title || len(w) != c.warnCount {
			t.Errorf("findSeparator(%q, %s, %v) = %q / %q %v", c.in, c.mode, c.list, a, ti, w)
		}
	}
}

func TestCleanupTitle(t *testing.T) {
	cases := []struct{ in, want string }{
		{"  Title  ", "Title"},
		{"- Title |", "Title"},
		{`"Title"`, "Title"},
		{"'Title'", "Title"},
		{`"Title'`, `"Title'`},
		{`" - Title "`, "Title"},
		{`"`, `"`},
		{"", ""},
		{"| - |", ""},
	}
	for _, c := range cases {
		if got := cleanupTitle(c.in); got != c.want {
			t.Errorf("cleanupTitle(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestProbe(t *testing.T) {
	e := newEngine(resolvedOptions{mode: requestedMode{kind: requestMode, mode: ModeTracklist}})
	p := e.probe("01 00:00 - Intro")
	if !p.position || p.empty {
		t.Errorf("probe position %+v", p)
	}
	p = e.probe("05:00 Noisia - Stigma")
	if p.position || !p.spacedDash || p.empty {
		t.Errorf("probe spaced dash %+v", p)
	}
	tr := p.finish(false)
	if tr.Timestamp == nil || tr.Timestamp.Seconds != 300 || tr.Title != "Stigma" {
		t.Errorf("probe finish %+v", tr)
	}
	p = e.probe("   ")
	if !p.empty {
		t.Errorf("probe empty %+v", p)
	}
}

func TestRunShapes(t *testing.T) {
	tr := Parse("")
	if tr.Artists == nil || tr.Versions == nil || tr.Junk == nil || tr.Warnings == nil {
		t.Error("empty input must give non-nil slices")
	}
	if tr.Mode != ModeClean {
		t.Errorf("mode %s", tr.Mode)
	}
	tr = Parse("01. Noisia - Stigma")
	if tr.Position == nil || tr.Position.Number != 1 || tr.Title != "Stigma" {
		t.Errorf("parse %+v", tr)
	}
	tr = ParseWith("Stigma", ParseOptions{Mode: "youtube", Uploader: "NoisiaVEVO"})
	if len(tr.Artists) != 1 || tr.Artists[0].Name != "Noisia" {
		t.Errorf("uploader fallback %+v", tr.Artists)
	}
	if ParseArtists("") == nil {
		t.Error("ParseArtists must give a non-nil slice")
	}
}

// FuzzRun asserts that the engine never panics and always gives valid UTF-8 (R0.8).
func FuzzRun(f *testing.F) {
	seeds := []string{
		"", " ", "-", " - ", "( [ {", ") ] }", "01. A - B", "[00:00] - x", "A | B || C",
		"x.mp3", "_-_", "Jay-Z-Numb", "A \"B\" (c)", "T by A", "\U0001f600 - \u00fc\u0301", "\ue000",
		"A feat. B prod. by C (x) - y", "\xff\xfe", "1:2:3", "99:99:99 - - - -",
	}
	for _, s := range seeds {
		f.Add(s, uint8(0))
	}
	engines := []*engine{
		sharedEngine(),
		newEngine(resolvedOptions{mode: requestedMode{kind: requestMode, mode: ModeYouTube}, uploader: "U - Topic"}),
		newEngine(resolvedOptions{mode: requestedMode{kind: requestMode, mode: ModeFilename}}),
		newEngine(resolvedOptions{mode: requestedMode{kind: requestMode, mode: ModeTracklist}, knownArtists: []string{"3 Doors Down", "\U0001f600"}}),
		newEngine(resolvedOptions{mode: requestedMode{kind: requestOther}}),
	}
	f.Fuzz(func(t *testing.T, input string, which uint8) {
		e := engines[int(which)%len(engines)]
		in := sanitize(input)
		tr := e.run(in, which%2 == 0)
		if !utf8.ValidString(tr.Title) || !utf8.ValidString(tr.FullTitle) {
			t.Fatalf("invalid UTF-8 output for %q", input)
		}
		_ = e.runArtists(in)
		p := e.probe(in)
		_ = p.finish(which%3 == 0)
		if n := normalize(in); normalize(n) != n {
			t.Fatalf("normalize not idempotent on %q", input)
		}
	})
}
