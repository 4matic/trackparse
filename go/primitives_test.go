package trackparse

// R0 primitives against values taken from the JS reference. The expected values are
// ../rust/tests/data/primitives.json, written by rust/scripts/dump_primitives.mjs from
// js/src/words.ts; it is read in place (not copied), and the table tests skip when it is
// absent. TRACKPARSE_PRIMITIVES=<file> swaps in another dump (the --all sweep). The letter and
// uppercase flags of the same file are tested by unicode_test.go.

import (
	"os"
	"slices"
	"testing"
)

func primitivesData(t *testing.T) jsonValue {
	t.Helper()
	path := os.Getenv("TRACKPARSE_PRIMITIVES")
	if path == "" {
		path = "../rust/tests/data/primitives.json"
	}
	text, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("%s not found", path)
	}
	v, err := parseJSON(text)
	if err != nil {
		t.Fatal(err)
	}
	return v
}

// codePoints is the dumped code points paired with one flag string ("0110...") of property.
func codePoints(t *testing.T, data jsonValue, property string) ([]rune, []bool) {
	t.Helper()
	table, _ := data.get("codePoints")
	runs, _ := table.get("runs")
	var cps []rune
	for _, run := range runs.arr {
		if len(run.arr) != 2 {
			t.Fatal("bad run")
		}
		first, count := rune(run.arr[0].num), int(run.arr[1].num)
		for k := 0; k < count; k++ {
			cps = append(cps, first+rune(k))
		}
	}
	flags, _ := table.getString(property)
	if len(cps) < 5000 || len(flags) != len(cps) {
		t.Fatalf("%s: %d code points, %d flags", property, len(cps), len(flags))
	}
	want := make([]bool, len(flags))
	for i := 0; i < len(flags); i++ {
		want[i] = flags[i] == '1'
	}
	return cps, want
}

func checkProperty(t *testing.T, property string, test func(rune) bool) {
	t.Helper()
	cps, want := codePoints(t, primitivesData(t), property)
	for i, r := range cps {
		if test(r) != want[i] {
			t.Errorf("%s of U+%04X: got %v", property, r, !want[i])
		}
	}
}

func TestPrimitivesWhitespaceSet(t *testing.T) {
	checkProperty(t, "whitespace", isWhitespace)
	// R0.2, spelled out: U+0085 is in; U+FEFF, U+200B and U+180E are out.
	var got []rune
	for r := rune(0); r <= 0x10ffff; r++ {
		if isWhitespace(r) {
			got = append(got, r)
		}
	}
	want := []rune{0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x85, 0xa0, 0x1680}
	for r := rune(0x2000); r <= 0x200a; r++ {
		want = append(want, r)
	}
	want = append(want, 0x2028, 0x2029, 0x202f, 0x205f, 0x3000)
	if !slices.Equal(got, want) {
		t.Errorf("whitespace set %U, want %U", got, want)
	}
	if isWhitespace(noRune) {
		t.Error("noRune is whitespace")
	}
}

func TestPrimitivesJSTrimSet(t *testing.T) {
	checkProperty(t, "jsSpace", isJSSpace)
	for _, c := range []struct{ in, want string }{
		{"\ufeff x\u00a0\u2028", "x"},
		{"\u0085x\u0085", "\u0085x\u0085"}, // R0.2 whitespace, not JS whitespace
	} {
		if got := jsTrim(c.in); got != c.want {
			t.Errorf("jsTrim(%q) = %q", c.in, got)
		}
	}
	if got := jsTrimEnd(" x \ufeff"); got != " x" {
		t.Errorf("jsTrimEnd = %q", got)
	}
	if got := string(jsTrimRunes([]rune("\u3000x y\t"))); got != "x y" {
		t.Errorf("jsTrimRunes = %q", got)
	}
}

func TestPrimitivesStrings(t *testing.T) {
	data := primitivesData(t)
	records, _ := data.get("strings")
	if len(records.arr) < 250 && os.Getenv("TRACKPARSE_PRIMITIVES") == "" {
		t.Fatalf("only %d records", len(records.arr))
	}
	for _, r := range records.arr {
		field := func(key string) string {
			s, ok := r.getString(key)
			if !ok {
				return "<missing>"
			}
			return s
		}
		s := field("s")
		check := func(name, got, want string) {
			t.Helper()
			if got != want {
				t.Errorf("%s(%q) = %q, JS %q", name, s, got, want)
			}
		}
		n, _ := r.get("utf16Len")
		if got := utf16Len([]rune(s)); got != int(n.num) || utf16LenString(s) != got {
			t.Errorf("utf16Len(%q) = %d, JS %v", s, got, n.num)
		}
		check("asciiLower", asciiLower(s), field("asciiLower"))
		check("jsToLower", jsToLower(s), field("lower"))
		check("wordKey", wordKey(s), field("wordKey"))
		check("dedupKey", dedupKey(s), field("dedupKey"))
		check("collapseSpaces", collapseSpaces(s), field("collapse"))
		check("collapseRunes", collapseRunes([]rune(s)), field("collapse"))
		check("jsTrim", jsTrim(s), field("jsTrim"))
		check("jsTrimEnd", jsTrimEnd(s), field("jsTrimEnd"))
		var words []string
		w, _ := r.get("words")
		for _, x := range w.arr {
			words = append(words, x.str)
		}
		if got := splitWords(s); !slices.Equal(got, words) && !(len(got) == 0 && len(words) == 0) {
			t.Errorf("splitWords(%q) = %q, JS %q", s, got, words)
		}
		year, _ := r.get("year")
		gotYear, ok := yearOf(s)
		if (year.kind == jsonNull) == ok || (ok && gotYear != int(year.num)) {
			t.Errorf("yearOf(%q) = %d %v, JS %s", s, gotYear, ok, show(year))
		}
	}
}

func TestPrimitivesWordKeys(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{"FEAT.", "feat"}, {"Remix!!", "remix!"}, {"É.", "É"}, // ASCII lowercase only
	} {
		if got := wordKey(c.in); got != c.want {
			t.Errorf("wordKey(%q) = %q", c.in, got)
		}
	}
	if !wordMatches("Feat.", "feat.") || !wordMatches("Feat.", "feat") || wordMatches("feat", "feat.") {
		t.Error("wordMatches")
	}
	if stripKeyPunct("a;") != "a" || stripKeyPunct("ä") != "ä" {
		t.Error("stripKeyPunct")
	}
	set := newWordSet([]string{"feat.", "remix"})
	if !inVocab("FEAT.", set) || !inVocab("Remix,", set) || inVocab("feat", set) {
		t.Error("inVocab")
	}
}

func TestPrimitivesDedupKeys(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{"İstanbul", "istanbul"}, // i + U+0307, the mark then removed
		{"STRAẞE", "straße"},
		{"ΟΔΥΣΣΕΑΣ", "οδυσσεας"}, // final sigma
		{"  Beyonce\u0301   KNOWLES ", "beyonce knowles"},
		{"Й", "и"},                 // U+0306 is in the removed range
		{"か\u3099", "か\u3099"},     // other marks stay
		{"\xffA", "�a"[0:3] + "a"}, // invalid UTF-8 cannot reach here, but must not panic
	} {
		if got := dedupKey(c.in); got != c.want {
			t.Errorf("dedupKey(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestDedupKeyInvalidUTF8(t *testing.T) {
	_ = dedupKey("\xffA\xe2\x82") // cannot reach here through the API, but must not panic
}

func TestPrimitivesRuneHelpers(t *testing.T) {
	rs := []rune("a𝐀b")
	if at(rs, 1) != '𝐀' || at(rs, 3) != noRune || at(rs, -1) != noRune || before(rs, 0) != noRune {
		t.Error("at / before")
	}
	if string(sub(rs, 1, 99)) != "𝐀b" || string(sub(rs, 5, 2)) != "" || string(tail(rs, 2)) != "b" {
		t.Error("sub / tail")
	}
	if utf16Len(rs) != 4 {
		t.Error("utf16Len")
	}
	if !isLetterAt(rs, 0) || isLetterAt(rs, 9) || isUpperAt(rs, 2) || !isUpperAt([]rune("A"), 0) {
		t.Error("letter helpers")
	}
	spans := wordSpans([]rune(" ab\u3000c "))
	if len(spans) != 2 || spans[0] != (wordSpan{"ab", 1, 3}) || spans[1] != (wordSpan{"c", 4, 5}) {
		t.Errorf("wordSpans = %v", spans)
	}
	if !slices.Equal(spanTexts(spans), []string{"ab", "c"}) {
		t.Error("spanTexts")
	}
}

func TestPrimitivesDigits(t *testing.T) {
	s := []rune("x0123:59:60:7")
	if digitRun(s, 1) != 4 || digitRun(s, 0) != 0 || digitRun(s, 99) != 0 {
		t.Error("digitRun")
	}
	if digitsValue([]rune("0123")) != 123 || digitsValue([]rune("99999999999999999999")) != 1<<40 {
		t.Error("digitsValue")
	}
	if n, ok := twoDigitsUpTo59(s, 6); !ok || n != 59 {
		t.Error("59")
	}
	if _, ok := twoDigitsUpTo59(s, 9); ok {
		t.Error("60")
	}
	if _, ok := twoDigitsUpTo59(s, 1); ok {
		t.Error("followed by a third digit")
	}
	if _, ok := twoDigitsUpTo59(s, 12); ok {
		t.Error("one digit")
	}
}

func TestSanitize(t *testing.T) {
	const r = "�"
	for _, c := range []struct{ in, want string }{
		{"Noisia - Stigma", "Noisia - Stigma"},
		{"a\xffb", "a" + r + "b"},
		{"\xc0\xaf", r + r},         // C0 is never a lead
		{"\xe0\x80\x80", r + r + r}, // E0 needs A0..BF next
		{"\xe0\xa0", r},             // truncated: one maximal subpart
		{"\xe2\x82\xac", "€"},
		{"\xe2\x82", r},
		{"\xe2\x82x", r + "x"},
		{"\xed\xa0\x80", r + r + r}, // surrogates are ill-formed
		{"\xf0\x90\x80", r},
		{"\xf0\x90\x80\x80", "\U00010000"},
		{"\xf4\x90\x80\x80", r + r + r + r}, // above U+10FFFF
		{"\xf5\x80", r + r},
		{"\x80\x80", r + r},
		{"\xf1\x80\x80\xe1\x80\xc2", r + r + r},
	} {
		if got := sanitize(c.in); got != c.want {
			t.Errorf("sanitize(%q) = %q, want %q", c.in, got, c.want)
		}
	}
}

func TestPhraseTable(t *testing.T) {
	tb := newPhraseTable[int]()
	tb.add("Official Video", 1)
	tb.add("video", 2)
	tb.add(" official  video ", 3)
	tb.add("", 4)
	words := []string{"Official", "VIDEO,", "x"}
	check := func(m phraseMatch[int], ok bool, value, length int) {
		t.Helper()
		if !ok || m.value != value || m.length != length {
			t.Errorf("got %v %v, want %d %d", m, ok, value, length)
		}
	}
	m, ok := tb.matchAt(words, 0)
	check(m, ok, 1, 2)
	m, ok = tb.matchAt(words, 1)
	check(m, ok, 2, 1)
	m, ok = tb.matchEnding(words, 2)
	check(m, ok, 1, 2)
	if _, ok := tb.matchEnding(words, 0); ok {
		t.Error("matchEnding at 0")
	}
	if _, ok := tb.matchAt(words, 3); ok {
		t.Error("matchAt past the end")
	}
	// The first entry for a key wins (keys are trimmed and lowercased, not collapsed).
	m, ok = tb.matchAt([]string{"official", "video"}, 0)
	check(m, ok, 1, 2)
}
