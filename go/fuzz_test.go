package trackparse

// Fuzz targets for the SPEC invariants and R13.11. Each is seeded with every relevant fixture
// input (when ../spec is there) and checks the same functions as the seeded property tests in
// properties_test.go. Without -fuzz, `go test` runs the seeds only; to fuzz one target:
//
//	go test -run '^$' -fuzz '^FuzzParse$' -fuzztime 60s
//
// Inputs are arbitrary bytes, so invalid UTF-8 is covered too: the API reads it with the WHATWG
// replacement rule, and the checks compare against the sanitized input.

import (
	goflag "flag"
	"hash/fnv"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

// fixtureCase is one shared fixture input, for seeding.
type fixtureCase struct {
	fn    string
	input string
	edits []byte // the JSON of the case's edits
}

// fixtureCases is every fixture case under ../spec/fixtures; nil when it is absent.
var fixtureCases = sync.OnceValue(func() []fixtureCase {
	dir := filepath.Join(specDir, "fixtures")
	var out []fixtureCase
	_ = filepath.WalkDir(dir, func(path string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() || !strings.HasSuffix(path, ".json") {
			return nil
		}
		text, err := os.ReadFile(path)
		if err != nil {
			return nil
		}
		data, err := parseJSON(text)
		if err != nil {
			return nil
		}
		cases, ok := data.get("cases")
		if !ok || cases.kind != jsonArray {
			return nil
		}
		defaults, _ := data.get("defaults")
		for _, c := range cases.arr {
			fn, ok := c.getString("fn")
			if !ok {
				if fn, ok = defaults.getString("fn"); !ok {
					fn = "parse"
				}
			}
			input, _ := c.getString("input")
			edits := []byte("[]")
			if e, ok := c.get("edits"); ok {
				edits = marshal(e)
			}
			out = append(out, fixtureCase{fn: fn, input: input, edits: edits})
		}
		return nil
	})
	return out
})

// fuzzing reports whether this run fuzzes (-fuzz given). Without it the seeds are only run
// once as ordinary tests, which the property tests already cover broadly: every 25th fixture
// input is enough there and keeps `go test -race` fast.
func fuzzing() bool {
	f := goflag.Lookup("test.fuzz")
	return f != nil && f.Value.String() != ""
}

// sampled is the fixture cases a run seeds with: all when fuzzing, every 25th otherwise.
func sampled() []fixtureCase {
	all := fixtureCases()
	if fuzzing() {
		return all
	}
	var out []fixtureCase
	for i := 0; i < len(all); i += 25 {
		out = append(out, all[i])
	}
	return out
}

// seedInputs is the fixture inputs of the given functions, without duplicates.
func seedInputs(fns ...string) []string {
	seen := map[string]bool{}
	var out []string
	for _, c := range sampled() {
		for _, fn := range fns {
			if c.fn == fn && !seen[c.input] {
				seen[c.input] = true
				out = append(out, c.input)
			}
		}
	}
	return out
}

// fuzzParseOptions are the option sets FuzzParse cycles through: every mode, an uploader,
// known artists, splitAnd values, and keywords with integer-like keys and odd strings.
var fuzzParseOptions = []ParseOptions{
	{},
	{Mode: ModeYouTube, Uploader: "Noisia"},
	{Mode: ModeFilename, KnownArtists: []string{"Camo & Krooked", "Chase & Status", " ", ""}},
	{Mode: ModeClean, SplitAnd: SplitAndNever},
	{Mode: ModeTracklist, SplitAnd: SplitAndAlways},
	{Mode: Mode("nonsense"), SplitAnd: SplitAnd("nonsense")},
	{Keywords: Keywords{
		VersionHeads: []VersionHead{{Key: "rerub", Type: VersionTypeRemix}, {Key: "2", Type: "x"}, {Key: "10", Type: VersionTypeDub}},
		Descriptors:  []string{"Deluxe", "", " "},
		Genres:       []string{"neurofunk", "x"},
		Junk:         []JunkPhrase{{Phrase: "out now", Kind: JunkKindPromo}, {Phrase: "1", Kind: "weird"}, {Phrase: "\ufeff", Kind: ""}},
		FeatMarkers:  []string{"with", "&", "-"},
	}},
}

// fuzzTracklistOptions are the option sets FuzzParseTracklist cycles through.
var fuzzTracklistOptions = []TracklistOptions{
	{},
	{Mode: ModeAuto},
	{Mode: ModeYouTube, AudioSeconds: Ptr(600.0)},
	{KnownArtists: []string{"Camo & Krooked"}, MarkerWords: []string{"Break", "", "ID"}},
	{SplitAnd: SplitAndNever, AudioSeconds: Ptr(-1.0)},
	{AudioSeconds: Ptr(0.0)},
	{Keywords: Keywords{Junk: []JunkPhrase{{Phrase: "out now", Kind: JunkKindPromo}}}},
}

// seedFrom is a generator seed from bytes (FNV-1a).
func seedFrom(b []byte) uint64 {
	h := fnv.New64a()
	_, _ = h.Write(b)
	return h.Sum64()
}

func FuzzParse(f *testing.F) {
	for i, s := range seedInputs("parse", "format", "normalize", "parseArtists") {
		f.Add(s, uint8(i))
	}
	for _, s := range trickyFragments {
		f.Add(s, uint8(0))
	}
	f.Fuzz(func(t *testing.T, s string, set uint8) {
		checkTrackTotality(t, s)
		options := fuzzParseOptions[int(set)%len(fuzzParseOptions)]
		track := ParseWith(s, options)
		if track.Input != sanitize(s) {
			t.Fatalf("%q: input %q", s, track.Input)
		}
		if options.Keywords.isEmpty() {
			// A keyword option may name any version type or junk kind, which the schema's
			// enums do not list (as in JavaScript).
			checkJSONSchema(t, "parse", marshal(&track), "parse with options")
		}
		checkArtistHygiene(t, s, &track)
		if a, b := string(marshal(&track)), func() string { x := NewParser(options).Parse(s); return string(marshal(&x)) }(); a != b {
			t.Fatalf("%q: ParseWith and Parser differ", s)
		}
	})
}

func FuzzParseArtists(f *testing.F) {
	for i, s := range seedInputs("parseArtists", "parse") {
		f.Add(s, uint8(i))
	}
	f.Fuzz(func(t *testing.T, s string, set uint8) {
		options := fuzzParseOptions[int(set)%len(fuzzParseOptions)]
		for _, artists := range [][]Artist{ParseArtists(s), ParseArtistsWith(s, options)} {
			if options.Keywords.isEmpty() {
				checkJSONSchema(t, "parseArtists", marshal(artistList(artists)), "parseArtists")
			}
			track := Track{Artists: artists}
			checkArtistHygiene(t, s, &track)
			for _, a := range artists {
				if a.Source != SourceArtist {
					t.Fatalf("%q: source %q", s, a.Source)
				}
			}
		}
	})
}

func FuzzFormat(f *testing.F) {
	for i, s := range seedInputs("format", "parse") {
		f.Add(s, uint64(i))
	}
	f.Fuzz(func(t *testing.T, s string, seed uint64) {
		r := newRNG(seed)
		options := fuzzParseOptions[r.below(len(fuzzParseOptions))]
		track := ParseWith(s, options)
		_ = Format(track)
		_ = FormatWith(track, formatOptionsFrom(r, s))
		_ = AllArtists(track)
		// R11.6 and the other track-shaped invariants, on a track drawn from the seed.
		checkShapedTrack(t, r)
	})
}

func FuzzParseTracklist(f *testing.F) {
	for i, s := range seedInputs("parseTracklist", "applyEdits") {
		f.Add(s, uint64(i))
	}
	f.Fuzz(func(t *testing.T, text string, seed uint64) {
		r := newRNG(seed)
		checkTracklist(t, text, TracklistOptions{})
		checkTracklist(t, text, fuzzTracklistOptions[r.below(len(fuzzTracklistOptions))])
		checkOneLine(t, text)
		checkEmphasis(t, writeTime(r.pick(timeStyles), r.below(3600), r.below(1000)), r.pick(emphasisRests))
	})
}

func FuzzApplyEdits(f *testing.F) {
	for i, c := range sampled() {
		if c.fn == "applyEdits" {
			f.Add(c.input, c.edits, uint64(i))
		}
	}
	for i, s := range seedInputs("parseTracklist") {
		f.Add(s, []byte("[]"), uint64(i))
	}
	f.Fuzz(func(t *testing.T, text string, edits []byte, seed uint64) {
		r := newRNG(seed)
		// Loose edits: the fuzzer's bytes when they are JSON, else a random list drawn from them.
		loose, err := parseJSON(edits)
		if err != nil {
			loose = newRNG(seedFrom(edits) ^ seed).looseEdits()
		}
		checkLooseEdits(t, text, loose)
		checkIdentity(t, text)
		checkLocality(t, text, r)
		checkInsertDelete(t, text, r)
		checkShiftRoundTrip(t, text, r)
		checkSort(t, text)
		// The same checks on a timed list drawn from the seed, where most edits apply.
		list := r.timedList()
		checkLocality(t, list, r)
		checkShiftRoundTrip(t, list, r)
	})
}
