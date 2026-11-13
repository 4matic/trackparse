package trackparse

// Tests of the artist side (classify.go, credits.go, artists.go, assemble.go, format.go) that
// run without the title side of the pipeline, against values dumped from the JS reference
// (testdata/artist_cases.json, written by scripts/dump_artist_cases.mjs).

import (
	"bytes"
	"math/rand"
	"os"
	"strings"
	"sync"
	"testing"
	"time"
)

var artistCases = sync.OnceValues(func() (jsonValue, error) {
	text, err := os.ReadFile("testdata/artist_cases.json")
	if err != nil {
		return jsonValue{}, err
	}
	return parseJSON(text)
})

// casesOf is one section of testdata/artist_cases.json.
func casesOf(t *testing.T, section string) []jsonValue {
	t.Helper()
	all, err := artistCases()
	if err != nil {
		t.Fatal(err)
	}
	v, ok := all.get(section)
	if !ok || v.kind != jsonArray || len(v.arr) == 0 {
		t.Fatalf("testdata/artist_cases.json: no %s", section)
	}
	return v.arr
}

// optionsOf is the case's options object ({} when absent).
func optionsOf(c jsonValue) jsonValue {
	if o, ok := c.get("options"); ok {
		return o
	}
	return jsonValue{kind: jsonObject}
}

// localRunArtists is R10 as the title-side parser.go will run it, minus R1: the input is
// already normalized.
func localRunArtists(e *engine, norm string) []Artist {
	if norm == "" {
		return []Artist{}
	}
	c := e.newCtx()
	s, _ := scan([]rune(norm), 0)
	side := parseArtistSide(&s, c)
	artists := dedupArtists(side.credits)
	for i := range artists {
		artists[i].Source = SourceArtist
	}
	return artists
}

func TestParseArtistsMatchesJS(t *testing.T) {
	failed := 0
	for _, c := range casesOf(t, "artists") {
		input, _ := c.getString("input")
		want, _ := c.get("expected")
		got := marshal(artistList(localRunArtists(newEngine(resolveParseJSON(optionsOf(c))), input)))
		if exp := marshal(want); !bytes.Equal(got, exp) {
			failed++
			if failed <= 20 {
				t.Errorf("%q %s\n got %s\nwant %s", input, marshal(optionsOf(c)), got, exp)
			}
		}
	}
	if failed > 0 {
		t.Errorf("%d artist lists differ", failed)
	}
}

func TestVersionsMatchJS(t *testing.T) {
	failed := 0
	for _, c := range casesOf(t, "versions") {
		want, _ := c.get("version")
		var d decoder
		v := d.version(want, "$")
		if d.err != nil {
			t.Fatal(d.err)
		}
		e := newEngine(resolveParseJSON(optionsOf(c)))
		cl := classify(v.Raw, site{source: SourceTitle, delimiter: v.Delimiter, pos: 0}, e.newCtx())
		var got []byte
		if cl.kind == classVersions && len(cl.versions) == 1 {
			out := toVersion(cl.versions[0])
			got = marshal(&out)
		} else {
			got = []byte("not one version")
		}
		if exp := marshal(want); !bytes.Equal(got, exp) {
			failed++
			if failed <= 20 {
				t.Errorf("%q\n got %s\nwant %s", v.Raw, got, exp)
			}
		}
	}
	if failed > 0 {
		t.Errorf("%d versions differ", failed)
	}
}

func TestFormatAndAllArtistsMatchJS(t *testing.T) {
	formats := 0
	for _, c := range casesOf(t, "tracks") {
		tv, _ := c.get("track")
		var d decoder
		track := d.track(tv, "$")
		if d.err != nil {
			t.Fatal(d.err)
		}
		list, _ := c.get("formats")
		for _, pair := range list.arr {
			if len(pair.arr) != 2 {
				t.Fatalf("bad formats entry %s", marshal(pair))
			}
			got := formatTrack(&track, resolveFormatJSON(pair.arr[0]))
			if want := pair.arr[1].str; got != want {
				t.Errorf("format(%q, %s)\n got %q\nwant %q", track.Input, marshal(pair.arr[0]), got, want)
			}
			formats++
		}
		want, _ := c.get("allArtists")
		if got := marshal(artistList(allArtists(&track))); !bytes.Equal(got, marshal(want)) {
			t.Errorf("allArtists(%q)\n got %s\nwant %s", track.Input, got, marshal(want))
		}
	}
	if formats < 1000 {
		t.Errorf("only %d format checks", formats)
	}
}

// TestFormatDefaults checks the typed door: the zero FormatOptions are the JS defaults.
func TestFormatDefaults(t *testing.T) {
	feat := "ft."
	amp := "&"
	track := Track{
		Title: "Out the Blue",
		Artists: []Artist{
			{Name: "Sub Focus", Role: RolePrimary, Source: SourceArtist},
			{Name: "Alice Gold", Role: RoleFeatured, Joiner: &feat, Source: SourceTitle},
			{Name: "Kele", Role: RoleFeatured, Joiner: &amp, Source: SourceTitle},
		},
		Versions: []Version{{Raw: "Wilkinson Remix", Delimiter: DelimiterParen}},
		Position: &Position{Raw: "03", Number: 3},
	}
	cases := []struct {
		o    FormatOptions
		want string
	}{
		{FormatOptions{}, "Sub Focus - Out the Blue (ft. Alice Gold & Kele) (Wilkinson Remix)"},
		{FormatOptions{Feat: FeatArtist}, "Sub Focus ft. Alice Gold & Kele - Out the Blue (Wilkinson Remix)"},
		{FormatOptions{Feat: FeatOmit, Versions: VersionsNone, Position: true}, "03. Sub Focus - Out the Blue"},
		{FormatOptions{Joiners: JoinersCanonical}, "Sub Focus - Out the Blue (feat. Alice Gold & Kele) (Wilkinson Remix)"},
		{FormatOptions{FeatMarker: "with"}, "Sub Focus - Out the Blue (with Alice Gold & Kele) (Wilkinson Remix)"},
	}
	for _, c := range cases {
		if got := FormatWith(track, c.o); got != c.want {
			t.Errorf("%+v\n got %q\nwant %q", c.o, got, c.want)
		}
	}
	if got := Format(Track{}); got != "" {
		t.Errorf("empty track: %q", got)
	}
}

func TestCleanName(t *testing.T) {
	cases := map[string]string{
		"  Noisia  ":          "Noisia",
		`"Camo & Krooked"`:    "Camo & Krooked",
		"'x'":                 "x",
		`"`:                   `"`,
		"Phace,;":             "Phace",
		"Phace , ":            "Phace",
		"by Skrillex":         "Skrillex",
		"By  Skrillex":        "Skrillex",
		"Bypass Skrillex":     "Bypass Skrillex",
		"by":                  "by",
		"":                    "",
		"'Noisia' & 'Mefjus'": "Noisia' & 'Mefjus", // only unwrapList checks inside
	}
	for in, want := range cases {
		if got := cleanName(in); got != want {
			t.Errorf("cleanName(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestSplitMultiParts(t *testing.T) {
	cases := map[string][]string{
		"A Remix / B Remix":         {"A Remix", "B Remix"},
		"A Remix; B Remix":          {"A Remix", "B Remix"},
		"A/B Remix":                 {"A/B Remix"},
		"A (x / y) Remix / B Remix": {"A (x / y) Remix", "B Remix"},
		"/ x":                       {"/ x"},
		"x /":                       {"x /"},
	}
	for in, want := range cases {
		got := splitMultiParts([]rune(in))
		if strings.Join(got, "|") != strings.Join(want, "|") {
			t.Errorf("splitMultiParts(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestClassifyKinds(t *testing.T) {
	c := sharedEngine().newCtx()
	s := site{source: SourceTitle, delimiter: DelimiterParen}
	cases := []struct {
		inner string
		kind  classKind
	}{
		{"", classUnknown},
		{"Official Video", classJunk},
		{"Official Video 2015", classJunk},
		{"feat. Alice Gold", classFeat},
		{"feat.", classUnknown},
		{"prod. by Noisia", classProducer},
		{"Explicit", classFlag},
		{"Radio Clean", classFlag},
		{"2015", classYear},
		{"1850", classUnknown},
		{"Skrillex Remix", classVersions},
		{"Remix by Skrillex", classVersions},
		{"Live at Wembley 1986", classVersions},
		{"A Remix / B VIP", classVersions},
		{"Hello World", classUnknown},
	}
	for _, x := range cases {
		if got := classify(x.inner, s, c); got.kind != x.kind {
			t.Errorf("classify(%q) = %d, want %d", x.inner, got.kind, x.kind)
		}
	}
	got := classify("A Remix / B VIP", s, c)
	if len(got.versions) != 2 || got.versions[1].vtype != VersionTypeVIP {
		t.Errorf("multi-version: %+v", got.versions)
	}
}

// TestArtistSideNeverPanics feeds random strings over a small alphabet of the characters the
// artist side reacts to through classify and parseArtistSide (R0.8).
func TestArtistSideNeverPanics(t *testing.T) {
	alphabet := []string{
		"(", ")", "[", "]", "{", "}", " ", " ", "a", "B", "&", ",", ";", "-", "'", `"`, "feat.", "ft",
		"x", "vs", "and", "with", "prod.", "by", "Remix", "Mix", "VIP", "2015", "ID", "1,000",
		"'s", "/", "é", "\U0001F600", "", "Official", "Video", "Live", " ",
	}
	r := rand.New(rand.NewSource(7))
	e := newEngine(resolveParse(&ParseOptions{KnownArtists: []string{"Chase & Status", "a B"}}))
	for range 20000 {
		var b strings.Builder
		for n := r.Intn(14); n > 0; n-- {
			b.WriteString(alphabet[r.Intn(len(alphabet))])
		}
		in := b.String()
		c := e.newCtx()
		_ = classify(collapseSpaces(in), site{source: SourceArtist, delimiter: DelimiterSquare, pos: 3}, c)
		artists := localRunArtists(e, in)
		track := Track{Artists: artists, Title: in}
		_ = formatTrack(&track, resolveFormat(&FormatOptions{Joiners: JoinersCanonical, Feat: FeatTitle}))
		_ = allArtists(&track)
	}
}

// ---------------------------------------------------------------------------------------------
// R9.2 dedup

// referenceDedup is R9.2 / R7.6 written the plain (quadratic) way, as in the JS reference:
// every "first remaining name of the list" is a scan. dedupArtists must give exactly this.
func referenceDedup(credits []credit) []Artist {
	entries := make([]dedupEntry, len(credits))
	for i, c := range credits {
		entries[i] = dedupEntry{a: toArtist(c), list: c.list, alive: true}
	}
	pass := func(e int, joiner *string) {
		list := entries[e].list
		var members []int
		for i := range entries {
			if entries[i].alive && entries[i].list == list {
				members = append(members, i)
			}
		}
		if len(members) == 0 || members[0] != e {
			return
		}
		if len(members) > 1 {
			entries[members[1]].a.Joiner = joiner
		}
	}
	index := map[string]int{}
	for i := range entries {
		key := dedupKeyOf(&entries[i].a)
		k, dup := index[key]
		if !dup {
			index[key] = i
			continue
		}
		if precedence(entries[i].a.Role) > precedence(entries[k].a.Role) {
			pass(k, entries[k].a.Joiner)
			entries[k].a.Role = entries[i].a.Role
			entries[k].a.Joiner = entries[i].a.Joiner
			entries[k].a.Source = entries[i].a.Source
			entries[k].list = entries[i].list
		} else {
			pass(i, entries[i].a.Joiner)
		}
		entries[i].alive = false
	}
	out := []Artist{}
	for _, e := range entries {
		if e.alive {
			out = append(out, e.a)
		}
	}
	return out
}

func joinerText(j *string) string {
	if j == nil {
		return "<nil>"
	}
	return *j
}

func artistsEqual(a, b []Artist) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i].Name != b[i].Name || a[i].Role != b[i].Role || a[i].Source != b[i].Source ||
			joinerText(a[i].Joiner) != joinerText(b[i].Joiner) {
			return false
		}
	}
	return true
}

// randomCredits is n credits over a few names, roles, sources, joiners and lists.
func randomCredits(r *rand.Rand, n, names, lists int) []credit {
	roles := []Role{RolePrimary, RoleFeatured, RoleProducer, RoleRemixer}
	sources := []Source{SourceArtist, SourceTitle, SourceVersion}
	pool := []string{"a", "A", "b", "c", "é", "é", "d", "D ", "f", "g"}
	if names > len(pool) {
		names = len(pool)
	}
	out := make([]credit, n)
	for i := range out {
		var j *string
		switch r.Intn(3) {
		case 1:
			s := "&"
			j = &s
		case 2:
			s := "j" + string(rune('0'+i%10))
			j = &s
		}
		out[i] = credit{
			name:   pool[r.Intn(names)],
			role:   roles[r.Intn(len(roles))],
			joiner: j,
			source: sources[r.Intn(len(sources))],
			pos:    i,
			list:   r.Intn(lists),
		}
	}
	return out
}

// TestDedupMatchesThePlainAlgorithm is rust/src/assemble.rs dedup_matches_the_plain_algorithm.
func TestDedupMatchesThePlainAlgorithm(t *testing.T) {
	r := rand.New(rand.NewSource(0x2545f491))
	for range 20000 {
		credits := randomCredits(r, r.Intn(14), 6, 1+r.Intn(4))
		if got, want := dedupArtists(credits), referenceDedup(credits); !artistsEqual(got, want) {
			t.Fatalf("credits %+v\n got %+v\nwant %+v", credits, got, want)
		}
	}
}

// TestDedupManyDuplicates compares with the reference on long lists made of thousands of
// duplicates, and checks the near-linear version stays fast where the plain one is quadratic.
func TestDedupManyDuplicates(t *testing.T) {
	r := rand.New(rand.NewSource(42))
	for _, shape := range []struct{ n, names, lists int }{
		{3000, 2, 1}, {3000, 3, 2}, {3000, 10, 40}, {4000, 1, 3}, {2000, 5, 1000},
	} {
		credits := randomCredits(r, shape.n, shape.names, shape.lists)
		if got, want := dedupArtists(credits), referenceDedup(credits); !artistsEqual(got, want) {
			t.Fatalf("shape %+v differs", shape)
		}
	}
	credits := randomCredits(r, 200000, 3, 2)
	start := time.Now()
	out := dedupArtists(credits)
	if d := time.Since(start); d > 2*time.Second {
		t.Errorf("200,000 credits took %v", d)
	}
	if len(out) == 0 || len(out) > 6 {
		t.Errorf("200,000 credits over 3 names gave %d artists", len(out))
	}
}

func TestDedupExamples(t *testing.T) {
	s := func(v string) *string { return &v }
	credits := []credit{
		{name: "Noisia", role: RolePrimary, source: SourceArtist, list: 0},
		{name: "Mefjus", role: RolePrimary, joiner: s("&"), source: SourceArtist, list: 0},
		{name: "noisia", role: RoleFeatured, joiner: s("feat."), source: SourceTitle, list: 1},
		{name: "Alix Perez", role: RoleFeatured, joiner: s("&"), source: SourceTitle, list: 1},
		{name: "Noisia", role: RoleProducer, joiner: s("prod."), source: SourceTitle, list: 2},
	}
	got := dedupArtists(credits)
	// The featured duplicate of Noisia goes, so Alix Perez inherits feat. (R7.6); the producer
	// is its own class (R9.2).
	want := []Artist{
		{Name: "Noisia", Role: RolePrimary, Source: SourceArtist},
		{Name: "Mefjus", Role: RolePrimary, Joiner: s("&"), Source: SourceArtist},
		{Name: "Alix Perez", Role: RoleFeatured, Joiner: s("feat."), Source: SourceTitle},
		{Name: "Noisia", Role: RoleProducer, Joiner: s("prod."), Source: SourceTitle},
	}
	if !artistsEqual(got, want) {
		t.Errorf("got %+v", got)
	}
	// Role upgrade: a later primary credit lifts an earlier featured one.
	up := dedupArtists([]credit{
		{name: "A", role: RoleFeatured, joiner: s("ft."), source: SourceTitle, list: 0},
		{name: "B", role: RoleFeatured, joiner: s("&"), source: SourceTitle, list: 0},
		{name: "a", role: RolePrimary, source: SourceArtist, list: 1},
	})
	wantUp := []Artist{
		{Name: "A", Role: RolePrimary, Source: SourceArtist},
		{Name: "B", Role: RoleFeatured, Joiner: s("ft."), Source: SourceTitle},
	}
	if !artistsEqual(up, wantUp) {
		t.Errorf("upgrade: got %+v", up)
	}
	if got := dedupArtists(nil); len(got) != 0 {
		t.Errorf("nil: %+v", got)
	}
}

func TestFullTitleOf(t *testing.T) {
	vs := []Version{
		{Raw: "VIP", Delimiter: DelimiterParen},
		{Raw: "x", Delimiter: DelimiterSquare},
		{Raw: "y", Delimiter: DelimiterCurly},
		{Raw: "Radio Edit", Delimiter: DelimiterDash},
	}
	if got := fullTitleOf("Stigma", vs); got != "Stigma (VIP) [x] {y} - Radio Edit" {
		t.Errorf("got %q", got)
	}
	if got := fullTitleOf("", vs[:1]); got != "(VIP)" {
		t.Errorf("got %q", got)
	}
}
