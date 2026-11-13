package trackparse

// Time ceilings (SPEC invariant 1 and R13.11.2) and benchmarks.
//
// The ceilings are generous on purpose: they catch super-linear blowups, not CI noise. Each
// timing is the best of three runs, and -race (several times slower) widens them. The other
// suites enforce 1 s (JS, Python) and 2 s (Rust) for 10,000 code points, and 3 s / 1 s for
// 10,000 lines (parseTracklist / shift + sort).
//
//	go test -run '^$' -bench . -benchmem

import (
	"fmt"
	"strings"
	"testing"
	"time"
)

// slowdown widens the ceilings under -race.
func slowdown() time.Duration {
	if raceEnabled {
		return 10
	}
	return 1
}

// bestOf3 is the fastest of three runs of f.
func bestOf3(f func()) time.Duration {
	best := time.Duration(1<<63 - 1)
	for i := 0; i < 3; i++ {
		start := time.Now()
		f()
		if d := time.Since(start); d < best {
			best = d
		}
	}
	return best
}

// repeated is block repeated to exactly n code points.
func repeated(block string, n int) string {
	rs := []rune(block)
	out := make([]rune, n)
	for i := range out {
		out[i] = rs[i%len(rs)]
	}
	return string(out)
}

// ceilingBlocks are the blocks of the JS suite plus the credit shapes that once made dedup
// quadratic.
var ceilingBlocks = []string{
	"(", "a - ", "A & B, ", "feat. X ", "[Remix] ", "-a", " by x", "\"q\" ", "xxxxxxx",
	"prod. A feat. B ", "a by me ", "(x) - ", "Official Video ", "x - Official Video ", "Remix - ",
	"a feat. ", "a & ", "(a remix) ",
	"Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [Official Video] ",
}

func TestCeilingTenThousandCodePoints(t *testing.T) {
	ceiling := 2 * time.Second * slowdown()
	for _, block := range ceilingBlocks {
		input := repeated(block, 10_000)
		for _, mode := range []Mode{ModeAuto, ModeYouTube, ModeFilename} {
			p := NewParser(ParseOptions{Mode: mode})
			if d := bestOf3(func() { p.Parse(input) }); d > ceiling {
				t.Errorf("%q in %q: %v (ceiling %v)", block, mode, d, ceiling)
			}
		}
	}
}

func TestCeilingRepeatedFeatCredits(t *testing.T) {
	// 1,250 `a feat. ` credits: 10,000 code points that all dedup to one name (R9.2).
	input := strings.Repeat("a feat. ", 1250)
	ceiling := 2 * time.Second * slowdown()
	if d := bestOf3(func() { Parse(input) }); d > ceiling {
		t.Errorf("parse: %v (ceiling %v)", d, ceiling)
	}
	if d := bestOf3(func() { ParseArtists(input) }); d > ceiling {
		t.Errorf("parseArtists: %v (ceiling %v)", d, ceiling)
	}
}

func TestCeilingRepeatedTrailingJunk(t *testing.T) {
	input := "A - T " + strings.Repeat("Official Video ", 2666)
	track := ParseWith(input, ParseOptions{Mode: ModeYouTube})
	if track.Title != "T" || len(track.Junk) != 2666 {
		t.Fatalf("title %q, %d junk", track.Title, len(track.Junk))
	}
	ceiling := 2 * time.Second * slowdown()
	if d := bestOf3(func() { ParseWith(input, ParseOptions{Mode: ModeYouTube}) }); d > ceiling {
		t.Errorf("%v (ceiling %v)", d, ceiling)
	}
}

func TestRepeatedFragmentsScaleBelowQuadratic(t *testing.T) {
	// Four times the input: linear work takes about 4 times as long, quadratic work 16 times.
	if testing.Short() || raceEnabled {
		t.Skip("timing ratios need an optimized, otherwise idle run")
	}
	for _, block := range []string{"a feat. ", "A feat. a, ", "(feat. a) ", "a & "} {
		small, large := repeated(block, 40_000), repeated(block, 160_000)
		credits := bestOf3(func() { ParseArtists(large) }).Seconds() / bestOf3(func() { ParseArtists(small) }).Seconds()
		track := bestOf3(func() { Parse(large) }).Seconds() / bestOf3(func() { Parse(small) }).Seconds()
		t.Logf("%q: ParseArtists x%.1f, Parse x%.1f", block, credits, track)
		if credits > 8 || track > 8 {
			t.Errorf("%q: 4 times the input takes ParseArtists x%.1f, Parse x%.1f", block, credits, track)
		}
	}
}

func hms(s int) string {
	return fmt.Sprintf("%02d:%02d:%02d", s/3600, (s%3600)/60, s%60)
}

// tenThousandLines is the 10,000-line list of the other suites.
func tenThousandLines() string {
	lines := make([]string, 10_000)
	for i := range lines {
		lines[i] = fmt.Sprintf("%s %d Artist %d & Other - Title %d (VIP Mix)", hms((i*37)%7200), i%100, i, i)
	}
	return strings.Join(lines, "\r\n")
}

func TestCeilingTenThousandLines(t *testing.T) {
	text := tenThousandLines()
	if n := len(ParseTracklist(text).Entries); n != 10_000 {
		t.Fatalf("%d entries", n)
	}
	parse := bestOf3(func() { ParseTracklist(text) })
	edits := []Edit{Shift(5), SortByTime()}
	edit := bestOf3(func() { ApplyEdits(text, edits) })
	t.Logf("10,000 lines: ParseTracklist %v, shift + sort %v", parse, edit)
	if c := 3 * time.Second * slowdown(); parse > c {
		t.Errorf("ParseTracklist took %v (ceiling %v)", parse, c)
	}
	if c := time.Second * slowdown(); edit > c {
		t.Errorf("ApplyEdits took %v (ceiling %v)", edit, c)
	}
}

func TestCeilingTwoThousandEdits(t *testing.T) {
	// R13.11.2: ApplyEdits costs lines plus edits, not their product.
	lines := make([]string, 5000)
	for i := range lines {
		lines[i] = fmt.Sprintf("%s A %d - T %d", hms(i*30), i, i)
	}
	text := strings.Join(lines, "\n")
	setTimes := make([]Edit, 2000)
	inserts := make([]Edit, 2000)
	for k := range setTimes {
		setTimes[k] = SetTime((k*7)%5000+1, float64(k*10))
		inserts[k] = Insert((k*7)%5000, "00:00 X - Y")
	}
	if n := len(ApplyEdits(text, setTimes).Changed); n != 2000 {
		t.Fatalf("set-time changed %d lines", n)
	}
	if n := len(ApplyEdits(text, inserts).Changed); n != 2000 {
		t.Fatalf("insert changed %d lines", n)
	}
	set := bestOf3(func() { ApplyEdits(text, setTimes) })
	ins := bestOf3(func() { ApplyEdits(text, inserts) })
	t.Logf("2,000 edits on 5,000 lines: set-time %v, insert %v", set, ins)
	c := time.Second * slowdown()
	if set > c || ins > c {
		t.Errorf("set-time %v, insert %v (ceiling %v)", set, ins, c)
	}
}

// ---------------------------------------------------------------------------------------------
// Benchmarks

const typicalTrack = "Noisia feat. Foreign Beggars - Shellshock (Phace Remix) [Official Video]"

func BenchmarkParse(b *testing.B) {
	for i := 0; i < b.N; i++ {
		Parse(typicalTrack)
	}
}

func BenchmarkParser(b *testing.B) {
	p := NewParser(ParseOptions{Mode: ModeYouTube, KnownArtists: []string{"Camo & Krooked"}})
	for i := 0; i < b.N; i++ {
		p.Parse(typicalTrack)
	}
}

func BenchmarkParse10k(b *testing.B) {
	for _, c := range []struct{ name, block string }{
		{"text", typicalTrack + " "},
		{"feat", "a feat. "},
	} {
		input := repeated(c.block, 10_000)
		b.Run(c.name, func(b *testing.B) {
			for i := 0; i < b.N; i++ {
				Parse(input)
			}
		})
	}
}

// corpusSizedList is a list the size of a podcast episode's: 40 timed lines, a header and a
// marker.
func corpusSizedList() string {
	lines := []string{"Tracklist:", "00:00 Intro"}
	for i := 1; i <= 40; i++ {
		lines = append(lines, fmt.Sprintf("%s %s feat. %s - Title %d (%s Remix)",
			hms(i*150), propArtists[i%len(propArtists)], propArtists[(i+3)%len(propArtists)], i,
			propRemixers[i%len(propRemixers)]))
	}
	return strings.Join(lines, "\n") + "\n"
}

func BenchmarkParseTracklist(b *testing.B) {
	for _, c := range []struct{ name, text string }{
		{"corpus-sized", corpusSizedList()},
		{"10k-lines", tenThousandLines()},
	} {
		b.Run(c.name, func(b *testing.B) {
			for i := 0; i < b.N; i++ {
				ParseTracklist(c.text)
			}
		})
	}
}

func BenchmarkApplyEdits(b *testing.B) {
	edits := []Edit{Shift(5), SortByTime()}
	for _, c := range []struct{ name, text string }{
		{"corpus-sized", corpusSizedList()},
		{"10k-lines", tenThousandLines()},
	} {
		b.Run(c.name, func(b *testing.B) {
			for i := 0; i < b.N; i++ {
				ApplyEdits(c.text, edits)
			}
		})
	}
}
