package trackparse

import (
	"math"
	"os"
	"path/filepath"
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

// The typed options can say everything the JSON options can: typedParse, typedFormat and
// typedTracklist map any JSON options object (a fixture's, or a loose one with wrong types) to
// typed options that resolve to the same thing. An empty string, which the typed options read
// as "the default", is mapped to another unknown name where JavaScript reads it as one.

// unknownName is a name no option knows; JavaScript reads "" and non-strings as such a name.
const unknownName = "?"

// typedKeywords is options.keywords as typed Keywords.
func typedKeywords(o jsonValue) Keywords {
	k, ok := o.get("keywords")
	if !ok || k.kind != jsonObject {
		return Keywords{}
	}
	var out Keywords
	for _, e := range stringEntries(k.get("versionHeads")) {
		out.VersionHeads = append(out.VersionHeads, VersionHead{Key: e.key, Type: VersionType(e.value)})
	}
	for _, e := range stringEntries(k.get("junk")) {
		out.Junk = append(out.Junk, JunkPhrase{Phrase: e.key, Kind: JunkKind(e.value)})
	}
	out.Descriptors = stringList(k.get("descriptors"))
	out.Genres = stringList(k.get("genres"))
	out.FeatMarkers = stringList(k.get("featMarkers"))
	return out
}

// given is o[key] unless it is missing or null (JS `??`).
func given(o jsonValue, key string) (jsonValue, bool) {
	v, ok := o.get(key)
	if !ok || v.kind == jsonNull {
		return jsonValue{}, false
	}
	return v, true
}

// typedParse is a JSON parse options object as ParseOptions.
func typedParse(o jsonValue) ParseOptions {
	var out ParseOptions
	if o.kind != jsonObject {
		return out
	}
	if m, ok := given(o, "mode"); ok {
		out.Mode = Mode(unknownName)
		if m.kind == jsonString && m.str != "" {
			out.Mode = Mode(m.str)
		}
	}
	if s, ok := given(o, "splitAnd"); ok {
		out.SplitAnd = SplitAndAlways
		if s.kind == jsonString && s.str != "" {
			out.SplitAnd = SplitAnd(s.str)
		}
	}
	if u, ok := o.get("uploader"); ok && u.kind == jsonString {
		out.Uploader = u.str
	}
	out.KnownArtists = stringList(o.get("knownArtists"))
	out.Keywords = typedKeywords(o)
	return out
}

// typedFormat is a JSON format options object as FormatOptions.
func typedFormat(o jsonValue) FormatOptions {
	var out FormatOptions
	if o.kind != jsonObject {
		return out
	}
	if f, ok := given(o, "feat"); ok {
		out.Feat = FeatOmit
		if f.kind == jsonString && f.str != "" {
			out.Feat = FeatPlacement(f.str)
		}
	}
	if m, ok := o.get("featMarker"); ok {
		out.FeatMarker = Ptr(jsString(m))
	}
	if j, ok := given(o, "joiners"); ok {
		out.Joiners = JoinersOriginal
		if j.kind == jsonString && j.str == "canonical" {
			out.Joiners = JoinersCanonical
		}
	}
	if v, ok := given(o, "versions"); ok {
		out.Versions = VersionsNone
		if v.kind == jsonString && v.str == "all" {
			out.Versions = VersionsAll
		}
	}
	if p, ok := given(o, "producers"); ok {
		out.OmitProducers = !truthy(p)
	}
	if p, ok := given(o, "position"); ok {
		out.Position = truthy(p)
	}
	return out
}

// typedTracklist is a JSON tracklist options object as TracklistOptions.
func typedTracklist(o jsonValue) TracklistOptions {
	var out TracklistOptions
	if o.kind != jsonObject {
		return out
	}
	if m, ok := o.get("mode"); ok && m.kind == jsonString {
		out.Mode = Mode(m.str)
	}
	if s, ok := o.get("splitAnd"); ok && s.kind == jsonString {
		out.SplitAnd = SplitAnd(s.str)
	}
	if n, ok := jsNumber(o.get("audioSeconds")); ok {
		out.AudioSeconds = Ptr(n)
	}
	out.KnownArtists = stringList(o.get("knownArtists"))
	out.MarkerWords = stringList(o.get("markerWords"))
	out.Keywords = typedKeywords(o)
	return out
}

// emptyKeywords reports whether k adds no word (a JSON keywords object can be empty).
func emptyKeywords(k *keywordLists) bool {
	return k == nil || len(k.versionHeads)+len(k.descriptors)+len(k.genres)+len(k.junk)+len(k.featMarkers) == 0
}

func equalResolved(a, b resolvedOptions) bool {
	if a.mode != b.mode || a.uploader != b.uploader || a.splitAnd != b.splitAnd ||
		!slices.Equal(a.knownArtists, b.knownArtists) || emptyKeywords(a.keywords) != emptyKeywords(b.keywords) {
		return false
	}
	if emptyKeywords(a.keywords) {
		return true
	}
	x, y := a.keywords, b.keywords
	return slices.Equal(x.versionHeads, y.versionHeads) && slices.Equal(x.junk, y.junk) &&
		slices.Equal(x.genres, y.genres) && slices.Equal(x.descriptors, y.descriptors) &&
		slices.Equal(x.featMarkers, y.featMarkers)
}

func equalFormat(a, b resolvedFormat) bool {
	return a.feat == b.feat && a.canonical == b.canonical && a.versions == b.versions &&
		a.producers == b.producers && a.position == b.position &&
		(a.featMarker == nil) == (b.featMarker == nil) &&
		(a.featMarker == nil || *a.featMarker == *b.featMarker)
}

func equalAudio(a, b *float64) bool {
	return (a == nil) == (b == nil) && (a == nil || *a == *b)
}

func equalTracklist(a, b resolvedTracklist) bool {
	return equalResolved(a.parse, b.parse) && equalAudio(a.audioSeconds, b.audioSeconds) &&
		slices.Equal(a.markerWords, b.markerWords)
}

// looseOptions are JSON options with every kind of odd value JavaScript accepts.
var looseOptions = []string{
	`{}`, `null`, `[1]`, `"x"`,
	`{"mode":"auto"}`, `{"mode":"youtube","uploader":"Noisia"}`, `{"mode":"bogus"}`, `{"mode":""}`,
	`{"mode":3}`, `{"mode":null}`, `{"mode":"tracklist"}`,
	`{"splitAnd":"never"}`, `{"splitAnd":"sometimes"}`, `{"splitAnd":""}`, `{"splitAnd":false}`,
	`{"splitAnd":null}`, `{"splitAnd":"always"}`,
	`{"uploader":""}`, `{"uploader":5}`,
	`{"knownArtists":["Chase & Status",1,null]}`, `{"knownArtists":"Chase & Status"}`,
	`{"keywords":"x"}`, `{"keywords":{}}`, `{"keywords":{"genres":"Neuro"}}`,
	`{"keywords":{"versionHeads":{"rerub":"remix","7":"x","b":1},"junk":{"out now":"promo"},"genres":["Neuro"],"descriptors":["Deluxe"],"featMarkers":["w/"]}}`,
	`{"audioSeconds":0}`, `{"audioSeconds":-1}`, `{"audioSeconds":3600}`, `{"audioSeconds":1e999}`,
	`{"audioSeconds":true}`, `{"audioSeconds":"60"}`, `{"audioSeconds":null}`,
	`{"markerWords":["Outro Speech",3]}`, `{"markerWords":"Break"}`,
}

var looseFormatOptions = []string{
	`{}`, `null`, `[1]`,
	`{"feat":"artist","featMarker":"ft."}`, `{"feat":"nope"}`, `{"feat":""}`, `{"feat":2}`,
	`{"feat":null}`, `{"feat":"omit"}`, `{"feat":"title"}`, `{"feat":"source"}`,
	`{"featMarker":""}`, `{"featMarker":null}`, `{"featMarker":3}`, `{"featMarker":[1,null,"a"]}`,
	`{"featMarker":{}}`, `{"featMarker":false}`,
	`{"joiners":"canonical","versions":"none"}`, `{"joiners":""}`, `{"joiners":1}`,
	`{"versions":"bogus"}`, `{"versions":""}`, `{"versions":null}`, `{"versions":"all"}`,
	`{"producers":false,"position":true}`, `{"producers":0,"position":"x"}`,
	`{"producers":null,"position":null}`, `{"producers":"","position":[]}`,
}

func TestTypedOptionsExpressLooseJSON(t *testing.T) {
	for _, text := range looseOptions {
		o := mustJSON(t, text)
		typed := typedParse(o)
		if a, b := resolveParse(&typed), resolveParseJSON(o); !equalResolved(a, b) {
			t.Errorf("parse %s: typed %+v, JSON %+v", text, a, b)
		}
		tl := typedTracklist(o)
		if a, b := resolveTracklist(&tl), resolveTracklistJSON(o); !equalTracklist(a, b) {
			t.Errorf("tracklist %s: typed %+v, JSON %+v", text, a, b)
		}
	}
	for _, text := range looseFormatOptions {
		o := mustJSON(t, text)
		typed := typedFormat(o)
		if a, b := resolveFormat(&typed), resolveFormatJSON(o); !equalFormat(a, b) {
			t.Errorf("format %s: typed %+v, JSON %+v", text, a, b)
		}
	}
}

// TestTypedOptionsExpressEveryFixture runs every fixture of parse, parseArtists, format and
// parseTracklist through the typed API with its options mapped as above: the output bytes must
// equal the JSON door's.
func TestTypedOptionsExpressEveryFixture(t *testing.T) {
	if _, err := os.Stat(specDir); err != nil {
		t.Skipf("%s not found: the fixtures are not in the module zip", specDir)
	}
	cases := 0
	for _, path := range fixtureFiles(t, filepath.Join(specDir, "fixtures")) {
		file := readJSONFile(t, path)
		defaults, _ := file.get("defaults")
		list, _ := file.get("cases")
		for _, c := range list.arr {
			fn, ok := c.getString("fn")
			if !ok {
				if fn, ok = defaults.getString("fn"); !ok {
					fn = "parse"
				}
			}
			input, _ := c.getString("input")
			options := merged(defaults, c, "options")
			formatOptions := merged(defaults, c, "formatOptions")
			var typed []byte
			switch fn {
			case "parse":
				track := ParseWith(input, typedParse(options))
				typed = marshal(&track)
			case "parseArtists":
				typed = marshal(artistList(ParseArtistsWith(input, typedParse(options))))
			case "format":
				track := ParseWith(input, typedParse(options))
				typed = marshal(wireString(FormatWith(track, typedFormat(formatOptions))))
			case "parseTracklist":
				list := ParseTracklistWith(input, typedTracklist(options))
				typed = marshal(&list)
			default:
				continue
			}
			cases++
			loose := wireCall(fn, input, options, formatOptions, jArr(nil))
			if string(typed) != string(loose) {
				id, _ := c.getString("id")
				t.Errorf("%s: typed options give\n%s\nJSON options give\n%s", id, typed, loose)
			}
		}
	}
	if cases < 10000 {
		t.Fatalf("only %d fixture cases", cases)
	}
}

func TestPtr(t *testing.T) {
	p := Ptr(0.0)
	q := Ptr(0.0)
	if p == q || *p != 0 {
		t.Error("Ptr must return a fresh pointer to the value")
	}
	if r := resolveTracklist(&TracklistOptions{AudioSeconds: Ptr(0.0)}); r.audioSeconds == nil || *r.audioSeconds != 0 {
		t.Error("AudioSeconds: Ptr(0.0) is an audio of length 0")
	}
	if r := resolveTracklist(&TracklistOptions{}); r.audioSeconds != nil || r.parse.mode != (requestedMode{kind: requestMode, mode: ModeTracklist}) {
		t.Errorf("TracklistOptions defaults: %+v", r)
	}
	if r := resolveTracklist(&TracklistOptions{Mode: ModeAuto}); r.parse.mode.kind != requestAuto {
		t.Error("TracklistOptions{Mode: ModeAuto} is auto")
	}
	if r := resolveTracklist(&TracklistOptions{AudioSeconds: Ptr(math.Inf(1))}); r.audioSeconds != nil {
		t.Error("infinite audio")
	}
	if r := resolveFormat(&FormatOptions{FeatMarker: Ptr("")}); r.featMarker == nil || *r.featMarker != "" {
		t.Error("FeatMarker: Ptr(\"\") is an empty marker")
	}
	if got := FormatWith(Parse("A feat. B - T"), FormatOptions{FeatMarker: Ptr("")}); got != "A  B - T" {
		t.Errorf("empty feat marker: %q", got)
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
