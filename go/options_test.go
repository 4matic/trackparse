package trackparse

import (
	"math"
	"slices"
	"testing"
)

func TestJSEntries(t *testing.T) {
	got := jsEntries([]kv{{"b", "1"}, {"10", "x"}, {"a", "2"}, {"2", "y"}, {"b", "3"}, {"01", "z"}, {"4294967295", "w"}})
	want := []kv{{"2", "y"}, {"10", "x"}, {"b", "3"}, {"a", "2"}, {"01", "z"}, {"4294967295", "w"}}
	if !slices.Equal(got, want) {
		t.Errorf("jsEntries = %v, want %v", got, want)
	}
}

func TestResolveParseTypedMatchesJSON(t *testing.T) {
	for _, c := range []struct {
		typed ParseOptions
		json  string
	}{
		{ParseOptions{}, `{}`},
		{ParseOptions{Mode: "auto"}, `{"mode":"auto"}`},
		{ParseOptions{Mode: ModeYouTube, Uploader: "Noisia"}, `{"mode":"youtube","uploader":"Noisia"}`},
		{ParseOptions{Mode: "bogus"}, `{"mode":"bogus"}`},
		{ParseOptions{SplitAnd: SplitAndNever}, `{"splitAnd":"never"}`},
		{ParseOptions{SplitAnd: "sometimes"}, `{"splitAnd":"sometimes"}`},
		{ParseOptions{KnownArtists: []string{"Chase & Status"}}, `{"knownArtists":["Chase & Status"]}`},
		{
			ParseOptions{Keywords: Keywords{
				VersionHeads: []VersionHead{{"rerub", VersionTypeRemix}, {"7", "x"}},
				Junk:         []JunkPhrase{{"out now", JunkKindPromo}},
				Genres:       []string{"Neuro"},
			}},
			`{"keywords":{"versionHeads":{"rerub":"remix","7":"x"},"junk":{"out now":"promo"},"genres":["Neuro"]}}`,
		},
	} {
		typed := resolveParse(&c.typed)
		loose := resolveParseJSON(mustJSON(t, c.json))
		if !equalResolved(typed, loose) {
			t.Errorf("%s: typed %+v, JSON %+v", c.json, typed, loose)
		}
	}
	// JSON-only fallbacks.
	if r := resolveParseJSON(mustJSON(t, `{"mode":3,"splitAnd":false,"keywords":"x","knownArtists":["a",1]}`)); r.mode.kind != requestOther || r.splitAnd != splitAlways || r.keywords != nil || !slices.Equal(r.knownArtists, []string{"a"}) {
		t.Errorf("fallbacks: %+v", r)
	}
	if r := resolveParseJSON(mustJSON(t, `[1]`)); r.mode.kind != requestAuto || r.splitAnd != splitAuto {
		t.Errorf("not an object: %+v", r)
	}
}

func equalResolved(a, b resolvedOptions) bool {
	if a.mode != b.mode || a.uploader != b.uploader || a.splitAnd != b.splitAnd ||
		!slices.Equal(a.knownArtists, b.knownArtists) || (a.keywords == nil) != (b.keywords == nil) {
		return false
	}
	if a.keywords == nil {
		return true
	}
	x, y := a.keywords, b.keywords
	return slices.Equal(x.versionHeads, y.versionHeads) && slices.Equal(x.junk, y.junk) &&
		slices.Equal(x.genres, y.genres) && slices.Equal(x.descriptors, y.descriptors) &&
		slices.Equal(x.featMarkers, y.featMarkers)
}

func TestResolveFormat(t *testing.T) {
	for _, c := range []struct {
		typed FormatOptions
		json  string
	}{
		{FormatOptions{}, `{}`},
		{FormatOptions{Feat: FeatArtist, FeatMarker: "ft."}, `{"feat":"artist","featMarker":"ft."}`},
		{FormatOptions{Feat: "nope"}, `{"feat":"nope"}`},
		{FormatOptions{Joiners: JoinersCanonical, Versions: VersionsNone}, `{"joiners":"canonical","versions":"none"}`},
		{FormatOptions{Versions: "bogus"}, `{"versions":"bogus"}`},
		{FormatOptions{OmitProducers: true, Position: true}, `{"producers":false,"position":true}`},
	} {
		typed, loose := resolveFormat(&c.typed), resolveFormatJSON(mustJSON(t, c.json))
		same := typed.feat == loose.feat && typed.canonical == loose.canonical &&
			typed.versions == loose.versions && typed.producers == loose.producers &&
			typed.position == loose.position && (typed.featMarker == nil) == (loose.featMarker == nil) &&
			(typed.featMarker == nil || *typed.featMarker == *loose.featMarker)
		if !same {
			t.Errorf("%s: typed %+v, JSON %+v", c.json, typed, loose)
		}
	}
	r := resolveFormatJSON(mustJSON(t, `{"featMarker":null,"producers":0,"position":"x"}`))
	if r.featMarker == nil || *r.featMarker != "null" || r.producers || !r.position {
		t.Errorf("JS fallbacks: %+v", r)
	}
	if jsString(mustJSON(t, `[1,null,"a",[2,3]]`)) != "1,,a,2,3" || jsString(mustJSON(t, `{}`)) != "[object Object]" {
		t.Error("jsString")
	}
}

func TestResolveTracklist(t *testing.T) {
	r := resolveTracklist(&TracklistOptions{})
	if r.parse.mode != (requestedMode{kind: requestMode, mode: ModeTracklist}) || r.audioSeconds != nil {
		t.Errorf("defaults: %+v", r)
	}
	r = resolveTracklist(&TracklistOptions{Mode: "auto", SplitAnd: "bogus", AudioSeconds: 3600})
	if r.parse.mode.kind != requestAuto || r.parse.splitAnd != splitAuto || r.audioSeconds == nil || *r.audioSeconds != 3600 {
		t.Errorf("typed: %+v", r)
	}
	if r := resolveTracklist(&TracklistOptions{AudioSeconds: math.Inf(1)}); r.audioSeconds != nil {
		t.Error("infinite audio")
	}
	j := resolveTracklistJSON(mustJSON(t, `{"mode":"bogus","uploader":"x","splitAnd":"never","audioSeconds":true,"markerWords":["Outro Speech",3]}`))
	if j.parse.mode.mode != ModeTracklist || j.parse.uploader != "" || j.parse.splitAnd != splitNever ||
		j.audioSeconds != nil || !slices.Equal(j.markerWords, []string{"Outro Speech"}) {
		t.Errorf("JSON: %+v", j)
	}
	if j := resolveTracklistJSON(mustJSON(t, `{"audioSeconds":0}`)); j.audioSeconds == nil || *j.audioSeconds != 0 {
		t.Error("JSON audioSeconds 0 is a length")
	}
	if j := resolveTracklistJSON(mustJSON(t, `{"audioSeconds":-1}`)); j.audioSeconds != nil {
		t.Error("negative audio")
	}
}

func TestTables(t *testing.T) {
	tb := defaultTables()
	if tb != defaultTables() || tablesFor(nil) != tb {
		t.Error("default tables are not shared")
	}
	words := []string{"Official", "Music", "Video"}
	if m, ok := tb.junkPhrases.matchAt(words, 0); !ok || m.value != "video" || m.length != 3 {
		t.Errorf("junk: %+v %v", m, ok)
	}
	if m, ok := tb.junkOrGenre.matchAt([]string{"Drum", "&", "Bass"}, 0); !ok || m.value != "genre" || m.length != 3 {
		t.Errorf("genre: %+v %v", m, ok)
	}
	if m, ok := tb.runWords.matchAt([]string{"dub"}, 0); !ok || m.value.typeCapable == nil || m.value.head == nil || m.value.descriptor == nil {
		t.Errorf("runWords dub: %+v %v", m, ok)
	}
	if tb.platformSuffixes[0] != " | Free Listening on SoundCloud" {
		t.Errorf("platform suffixes: %q", tb.platformSuffixes)
	}
	if !isUnknownToken("ID", tb) || isUnknownToken("id", tb) || !isUnknownToken("UNKNOWN", tb) {
		t.Error("isUnknownToken")
	}
	kw := resolveKeywords(&Keywords{VersionHeads: []VersionHead{{"Rerub", VersionTypeRemix}}, FeatMarkers: []string{" W/ "}})
	k := tablesFor(kw)
	if m, ok := k.heads.matchAt([]string{"RERUB"}, 0); !ok || m.value != "remix" {
		t.Error("keyword head")
	}
	if _, ok := k.featMarkers.matchAt([]string{"w/"}, 0); !ok {
		t.Error("keyword feat marker")
	}
	if _, ok := tb.heads.matchAt([]string{"rerub"}, 0); ok {
		t.Error("keywords leaked into the default tables")
	}
}

func TestMatchKnownAt(t *testing.T) {
	e := newEngine(resolveParse(&ParseOptions{KnownArtists: []string{"Chase & Status", "Björk"}}))
	c := e.newCtx()
	text := []rune("chase & STATUS, Bjo\u0308rk-x")
	if n := matchKnownAt(text, 0, c, noRune); n != 14 {
		t.Errorf("Chase & Status: %d", n)
	}
	if n := matchKnownAt(text, 16, c, noRune); n != 0 {
		t.Errorf("Björk without the - boundary: %d", n)
	}
	if n := matchKnownAt(text, 16, c, '-'); n != 6 {
		t.Errorf("Björk with the - boundary: %d", n)
	}
	if !isKnownArtist("CHASE & status", c) || isKnownArtist("Chase", c) {
		t.Error("isKnownArtist")
	}
	c.warn(WarningNoSeparator)
	c.warn(WarningNoSeparator)
	if len(c.warnings) != 1 {
		t.Error("warn twice")
	}
}

func TestAPISanitizesAndNeverPanics(t *testing.T) {
	// With the pipeline stubbed this only checks the boundary: invalid UTF-8 is replaced.
	if got := Parse("a\xe2\x82b").Input; got != "a�b" {
		t.Errorf("Input = %q", got)
	}
	var p Parser
	_ = p.Parse("x")
	var np *Parser
	_ = np.ParseArtists("x")
	_ = ApplyEdits("00:00 A\n", []Edit{SetRest(1, "\xff"), Delete(-1)})
	_ = ParseTracklistWith("\xff", TracklistOptions{MarkerWords: []string{"\xff"}})
}
