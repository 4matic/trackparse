package trackparse

// Tests for unicode.go against Node, the reference engine.
//
// testdata/unicode/cases.jsonl is a committed sample (about 1.3 MB) written by
// scripts/gen_unicode.mjs from Node, so `go test` needs no Node. `gen_unicode.mjs --check` keeps
// it in step with the tables. The exhaustive check (every code point, every ordered pair of
// non-starters, ...) is too big to commit: with TRACKPARSE_UNICODE_FULL=1 the test runs
// `node scripts/gen_unicode.mjs --cases <tmp>` and compares everything it writes.

import (
	"bufio"
	"encoding/json"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"testing"
	"unicode/utf8"
)

type unicodeCase struct {
	G string  `json:"g"`
	S string  `json:"s"`
	C *string `json:"c"`
	D *string `json:"d"`
	L *string `json:"l"`
	K *int    `json:"k"`
}

func uniOrSelf(p *string, s string) string {
	if p == nil {
		return s
	}
	return *p
}

// checkUnicodeCases runs every case in path and reports failures, with counts per group.
func checkUnicodeCases(t *testing.T, path string) {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	counts := map[string]int{}
	checks := 0
	failures := 0
	fail := func(c *unicodeCase, what, got, want string) {
		failures++
		if failures <= 20 {
			t.Errorf("[%s] %s(%+q) = %+q, want %+q", c.G, what, c.S, got, want)
		}
	}
	for sc.Scan() {
		var c unicodeCase
		if err := json.Unmarshal(sc.Bytes(), &c); err != nil {
			t.Fatalf("%s: %v", path, err)
		}
		counts[c.G]++
		if got, want := nfc(c.S), uniOrSelf(c.C, c.S); got != want {
			fail(&c, "nfc", got, want)
		}
		if got, want := nfd(c.S), uniOrSelf(c.D, c.S); got != want {
			fail(&c, "nfd", got, want)
		}
		if got, want := jsToLower(c.S), uniOrSelf(c.L, c.S); got != want {
			fail(&c, "jsToLower", got, want)
		}
		checks += 3
		if c.K != nil {
			r, _ := utf8.DecodeRuneInString(c.S)
			if got, want := isLetter(r), *c.K&1 != 0; got != want {
				fail(&c, "isLetter", strconv.FormatBool(got), strconv.FormatBool(want))
			}
			if got, want := isUpperLetter(r), *c.K&2 != 0; got != want {
				fail(&c, "isUpperLetter", strconv.FormatBool(got), strconv.FormatBool(want))
			}
			checks += 2
		}
	}
	if err := sc.Err(); err != nil {
		t.Fatal(err)
	}
	groups := make([]string, 0, len(counts))
	total := 0
	for g, n := range counts {
		groups = append(groups, g)
		total += n
	}
	sort.Strings(groups)
	var b strings.Builder
	for _, g := range groups {
		b.WriteString(" ")
		b.WriteString(g)
		b.WriteString("=")
		b.WriteString(strconv.Itoa(counts[g]))
	}
	t.Logf("%d cases, %d checks, %d failures:%s", total, checks, failures, b.String())
	if total == 0 {
		t.Fatalf("%s: no cases", path)
	}
}

func TestUnicodeSample(t *testing.T) {
	path := filepath.Join("testdata", "unicode", "cases.jsonl")
	if _, err := os.Stat(path); err != nil {
		t.Skipf("%s not found: run node scripts/gen_unicode.mjs", path)
	}
	checkUnicodeCases(t, path)
}

func TestUnicodeFull(t *testing.T) {
	if os.Getenv("TRACKPARSE_UNICODE_FULL") != "1" {
		t.Skip("set TRACKPARSE_UNICODE_FULL=1 to compare every code point and more against Node")
	}
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node not found: the exhaustive check needs Node to compute the expected values")
	}
	dir := t.TempDir()
	cmd := exec.Command(node, filepath.Join("scripts", "gen_unicode.mjs"), "--cases", dir)
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		t.Fatalf("gen_unicode.mjs --cases: %v", err)
	}
	v, err := os.ReadFile(filepath.Join(dir, "unicode-version.txt"))
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.TrimSpace(string(v)); got != unicodeVersion {
		t.Fatalf("this Node has Unicode %s, the tables are Unicode %s: compare with the same version", got, unicodeVersion)
	}
	checkUnicodeCases(t, filepath.Join(dir, "cases.jsonl"))
}

func TestJSToLower(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{"", ""},
		{"abc", "abc"},
		{"ABC Def", "abc def"},
		{"\u039f\u0394\u039f\u03a3", "\u03bf\u03b4\u03bf\u03c2"},   // final sigma
		{"\u039f\u0394\u039f\u03a3A", "\u03bf\u03b4\u03bf\u03c3a"}, // a cased letter follows
		{"\u03a3", "\u03c3"},         // nothing cased before
		{"A\u03a3.", "a\u03c2."},     // "." is case-ignorable
		{"\u0130", "i\u0307"},        // several code points
		{"\u00c9COLE", "\u00e9cole"}, // Latin-1
		{"\U00010400", "\U00010428"}, // astral (Deseret)
		{"a\xffB", "a\ufffdb"},       // invalid UTF-8
	} {
		if got := jsToLower(c.in); got != c.want {
			t.Errorf("jsToLower(%+q) = %+q, want %+q", c.in, got, c.want)
		}
	}
}

func TestNormalizeExamples(t *testing.T) {
	for _, c := range []struct{ in, c, d string }{
		{"", "", ""},
		{"abc", "abc", "abc"},
		{"e\u0301", "\u00e9", "e\u0301"},
		{"\u00e9", "\u00e9", "e\u0301"},
		{"\u1e0b\u0323", "\u1e0d\u0307", "d\u0323\u0307"},              // reordering, then composition
		{"q\u0307\u0323", "q\u0323\u0307", "q\u0323\u0307"},            // no composite
		{"\u212b", "\u00c5", "A\u030a"},                                // singleton
		{"\u0344", "\u0308\u0301", "\u0308\u0301"},                     // non-starter decomposition
		{"\uac00", "\uac00", "\u1100\u1161"},                           // Hangul LV
		{"\u1100\u1161\u11a8", "\uac01", "\u1100\u1161\u11a8"},         // Hangul L V T
		{"\uac00\u11a7", "\uac00\u11a7", "\u1100\u1161\u11a7"},         // T base does not compose
		{"\u0b47\u0b3e", "\u0b4b", "\u0b47\u0b3e"},                     // two starters compose
		{"a\xffe\u0301", "a\ufffd\u00e9", "a\ufffde\u0301"},            // invalid UTF-8
		{"\u0301e", "\u0301e", "\u0301e"},                              // leading mark
		{"A\u0301\u0301", "\u00c1\u0301", "A\u0301\u0301"},             // the second mark is blocked
		{"\U0001d15e", "\U0001d157\U0001d165", "\U0001d157\U0001d165"}, // excluded composite
	} {
		if got := nfc(c.in); got != c.c {
			t.Errorf("nfc(%+q) = %+q, want %+q", c.in, got, c.c)
		}
		if got := nfd(c.in); got != c.d {
			t.Errorf("nfd(%+q) = %+q, want %+q", c.in, got, c.d)
		}
	}
}

func TestCategories(t *testing.T) {
	for _, c := range []struct {
		r             rune
		letter, upper bool
	}{
		{'a', true, false}, {'Z', true, true}, {'1', false, false}, {'_', false, false},
		{0xc9, true, true}, {0xe9, true, false}, {0x1c5, true, false}, // Lt is a letter, not Lu
		{0x2160, false, false}, // Nl: not L (unlike char::is_alphabetic)
		{0x0301, false, false}, {0x4e00, true, false}, {0x10400, true, true},
		{-1, false, false}, {0x110000, false, false}, {utf8.RuneError, false, false},
	} {
		if got := isLetter(c.r); got != c.letter {
			t.Errorf("isLetter(%U) = %v", c.r, got)
		}
		if got := isUpperLetter(c.r); got != c.upper {
			t.Errorf("isUpperLetter(%U) = %v", c.r, got)
		}
	}
}

// Normalization and lowercasing are total, and NFC/NFD have the algebraic properties of
// UAX #15 on random input, invalid UTF-8 included.
func TestUnicodeTotalAndStable(t *testing.T) {
	rng := rand.New(rand.NewSource(1))
	pool := []rune{'a', 'A', 'e', 0x3a3, 0x301, 0x323, 0x345, 0x334, 0xe9, 0x1e0b, 0x1100, 0x1161,
		0x11a8, 0xac00, 0x212b, 0x344, 0xf900, 0x1d15e, 0x0b47, 0x0b3e, 0x130, 0x2e, 0x1f600}
	for i := 0; i < 20000; i++ {
		var b []byte
		for k := rng.Intn(16); k > 0; k-- {
			if rng.Intn(10) == 0 {
				b = append(b, byte(0x80+rng.Intn(0x80))) // a stray byte
				continue
			}
			b = utf8.AppendRune(b, pool[rng.Intn(len(pool))])
		}
		s := string(b)
		c, d := nfc(s), nfd(s)
		_ = jsToLower(s)
		if !utf8.ValidString(c) || !utf8.ValidString(d) {
			t.Fatalf("%+q: invalid UTF-8 out", s)
		}
		if nfc(c) != c || nfd(d) != d || nfc(d) != c || nfd(c) != d {
			t.Fatalf("%+q: nfc %+q nfd %+q not stable", s, c, d)
		}
	}
	// A long run of marks in reverse class order takes the stable sort path.
	long := "a" + strings.Repeat("\u0301\u0323\u0345\u0334", 2500)
	if got := nfd(long); !strings.HasPrefix(got, "a\u0334\u0334") || nfd(got) != got {
		t.Errorf("long run of marks not ordered")
	}
}

func TestNormalizeNoAllocWhenNormalized(t *testing.T) {
	for _, s := range []string{
		"Noisia feat. Foreign Beggars - Shellshock (Original Mix)",
		"Bj\u00f6rk - J\u00f3ga",
		"\u0421\u043f\u043b\u0438\u043d - \u0413\u0440\u0430\u043d\u0430\u0442\u043e\u0432\u044b\u0439 \u0430\u043b\u044c\u0431\u043e\u043c",
		"\uc544\uc774\uc720 - \uc88b\uc740 \ub0a0",
	} {
		if n := testing.AllocsPerRun(100, func() { _ = nfc(s) }); n != 0 {
			t.Errorf("nfc(%q): %v allocations", s, n)
		}
	}
	ascii := "Noisia feat. Foreign Beggars - Shellshock"
	if n := testing.AllocsPerRun(100, func() { _ = nfd(ascii); _ = jsToLower("noisia") }); n != 0 {
		t.Errorf("ascii: %v allocations", n)
	}
}

var uniBenchSink string

func BenchmarkNFCASCII(b *testing.B) {
	s := "Noisia feat. Foreign Beggars - Shellshock (Original Mix) [Vision Recordings]"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfc(s)
	}
}

func BenchmarkNFCLatinNormalized(b *testing.B) {
	s := "Bj\u00f6rk & Sigur R\u00f3s - J\u00f3ga (Rem\u00edx) \u00c9dition sp\u00e9ciale"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfc(s)
	}
}

func BenchmarkNFCDecomposed(b *testing.B) {
	s := "Bjo\u0308rk & Sigur Ro\u0301s - Jo\u0301ga (Remi\u0301x) E\u0301dition spe\u0301ciale"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfc(s)
	}
}

func BenchmarkNFDLatin(b *testing.B) {
	s := "Bj\u00f6rk & Sigur R\u00f3s - J\u00f3ga (Rem\u00edx) \u00c9dition sp\u00e9ciale"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfd(s)
	}
}

func BenchmarkNFCHangul(b *testing.B) {
	s := "\uc544\uc774\uc720 - \uc88b\uc740 \ub0a0 (\ud53c\uccd0\ub9c1 \uac00\uc218)"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfc(s)
	}
}

func BenchmarkJSToLower(b *testing.B) {
	s := "\u039f\u0394\u03a5\u03a3\u03a3\u0395\u0391\u03a3 feat. BJ\u00d6RK"
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = jsToLower(s)
	}
}

// 10,000 code points of mixed text, the size of the R0.8 speed check.
func BenchmarkNFC10k(b *testing.B) {
	rng := rand.New(rand.NewSource(2))
	pool := []rune{'a', 'B', ' ', '-', 0xe9, 0x301, 0x323, 0x3a3, 0x430, 0xac00, 0x1100, 0x1161, 0x4e00, 0x1f600}
	rs := make([]rune, 10000)
	for i := range rs {
		rs[i] = pool[rng.Intn(len(pool))]
	}
	s := string(rs)
	b.SetBytes(int64(len(s)))
	for i := 0; i < b.N; i++ {
		uniBenchSink = nfc(s)
	}
}
