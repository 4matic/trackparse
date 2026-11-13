package trackparse

import (
	"encoding/json"
	"math"
	"strings"
	"testing"
)

func TestJSNumber(t *testing.T) {
	// Expected values from Node: JSON.stringify(n).
	for _, c := range []struct {
		n    float64
		want string
	}{
		{0, "0"}, {math.Copysign(0, -1), "0"}, {1, "1"}, {-1, "-1"}, {3167, "3167"},
		{3723.5, "3723.5"}, {0.1, "0.1"}, {0.000001, "0.000001"}, {0.0000001, "1e-7"},
		{1e21, "1e+21"}, {1e20, "100000000000000000000"},
		{123456789012345680000, "123456789012345680000"}, {1.5e-7, "1.5e-7"},
		{-2.5e300, "-2.5e+300"}, {5e-324, "5e-324"},
		{1.7976931348623157e308, "1.7976931348623157e+308"},
		{0.30000000000000004, "0.30000000000000004"}, {100, "100"}, {0.000003, "0.000003"},
		{12345.6789, "12345.6789"}, {9007199254740992, "9007199254740992"},
		{18446744073709551616, "18446744073709552000"},
		{math.NaN(), "null"}, {math.Inf(1), "null"}, {math.Inf(-1), "null"},
	} {
		if got := string(appendJSNumber(nil, c.n)); got != c.want {
			t.Errorf("appendJSNumber(%v) = %s, want %s", c.n, got, c.want)
		}
	}
}

func TestJSString(t *testing.T) {
	in := "a\"\\\b\f\n\r\t\x00\x1f\x7f<>&  é😀"
	// JSON.stringify of the same string in Node.
	want := "\"a\\\"\\\\\\b\\f\\n\\r\\t\\u0000\\u001f\x7f<>&  é😀\""
	if got := string(appendJSString(nil, in)); got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
	if got := string(appendJSString(nil, "a\xffb")); got != "\"a�b\"" {
		t.Errorf("invalid UTF-8: %q", got)
	}
}

func TestJSONReader(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{`{"a":1,"b":[true,false,null],"c":"x"}`, `{"a":1,"b":[true,false,null],"c":"x"}`},
		{` [ 1 , 2.50 , -0 , 1e999 , -1e999 ] `, `[1,2.5,0,null,null]`},
		{`{"a":1,"b":2,"a":3}`, `{"a":3,"b":2}`}, // first place, last value
		{`"😀"`, `"😀"`},
		{`"\ud800"`, "\"�\""},
		{`"\udc00x"`, "\"�x\""},
		{`"\ud800A"`, "\"�A\""},
		{`"\ud800\ud800"`, "\"��\""},
		{`"\/\q\é"`, `"/qé"`},
		{"\"a\xffb\"", "\"a�b\""},
	} {
		v, err := parseJSON([]byte(c.in))
		if err != nil {
			t.Errorf("parseJSON(%s): %v", c.in, err)
			continue
		}
		if got := show(v); got != c.want {
			t.Errorf("parseJSON(%s) = %s, want %s", c.in, got, c.want)
		}
	}
	for _, bad := range []string{``, `{`, `[1,]`, `{"a" 1}`, `nul`, `"abc`, `1 2`, `-`, `"\u12"`,
		strings.Repeat("[", 300) + strings.Repeat("]", 300)} {
		if _, err := parseJSON([]byte(bad)); err == nil {
			t.Errorf("parseJSON(%q) should fail", bad)
		}
	}
	if v, _ := parseJSON([]byte(`1e999`)); !math.IsInf(v.num, 1) {
		t.Error("1e999 is not infinity")
	}
}

func sampleTrack() Track {
	feat, desc := "feat.", "at Lowlands"
	year, vyear := 2015, 2016
	return Track{
		Input:     "01. Noisia feat. Foreign Beggars - Stigma (Phace Remix) [HD]",
		Mode:      ModeYouTube,
		Position:  &Position{Raw: "01", Number: 1},
		Timestamp: &Timestamp{Raw: "01:23", Seconds: 83},
		Artists: []Artist{
			{Name: "Noisia", Role: RolePrimary, Source: SourceArtist},
			{Name: "Foreign Beggars", Role: RoleFeatured, Joiner: &feat, Source: SourceArtist},
		},
		Title:     "Stigma",
		FullTitle: "Stigma (Phace Remix)",
		Versions: []Version{{
			Type: VersionTypeRemix, Raw: "Phace Remix",
			Artists:   []Artist{{Name: "Phace", Role: RoleRemixer, Source: SourceVersion}},
			Modifiers: []string{"extended"}, Descriptor: &desc, Year: &vyear,
			UnknownArtist: true, Delimiter: DelimiterParen,
		}},
		Year:     &year,
		Flags:    Flags{Explicit: true, UnknownTitle: true},
		Junk:     []Junk{{Raw: "HD", Kind: JunkKindQuality}},
		Warnings: []Warning{WarningNoSeparator, "somethingNew"},
	}
}

func TestTrackJSONRoundTrip(t *testing.T) {
	tr := sampleTrack()
	b, err := tr.MarshalJSON()
	if err != nil {
		t.Fatal(err)
	}
	want := `{"input":"01. Noisia feat. Foreign Beggars - Stigma (Phace Remix) [HD]","mode":"youtube",` +
		`"position":{"raw":"01","number":1},"timestamp":{"raw":"01:23","seconds":83},` +
		`"artists":[{"name":"Noisia","role":"primary","joiner":null,"source":"artist"},` +
		`{"name":"Foreign Beggars","role":"featured","joiner":"feat.","source":"artist"}],` +
		`"title":"Stigma","fullTitle":"Stigma (Phace Remix)","versions":[{"type":"remix",` +
		`"raw":"Phace Remix","artists":[{"name":"Phace","role":"remixer","joiner":null,` +
		`"source":"version"}],"modifiers":["extended"],"descriptor":"at Lowlands","year":2016,` +
		`"unknownArtist":true,"delimiter":"("}],"year":2015,"flags":{"explicit":true,` +
		`"clean":false,"unknownArtist":false,"unknownTitle":true},"junk":[{"raw":"HD",` +
		`"kind":"quality"}],"warnings":["noSeparator","somethingNew"]}`
	if string(b) != want {
		t.Errorf("MarshalJSON:\n%s\nwant\n%s", b, want)
	}
	var back Track
	if err := json.Unmarshal(b, &back); err != nil {
		t.Fatal(err)
	}
	b2, _ := back.MarshalJSON()
	if string(b2) != want {
		t.Errorf("round trip:\n%s", b2)
	}
	// encoding/json calls MarshalJSON; the value is the same.
	viaStd, err := json.Marshal(tr)
	if err != nil || string(viaStd) != want {
		t.Errorf("json.Marshal: %s %v", viaStd, err)
	}
	// An empty track still has every key, with [] for lists.
	empty, _ := Track{}.MarshalJSON()
	if string(empty) != `{"input":"","mode":"","position":null,"timestamp":null,"artists":[],`+
		`"title":"","fullTitle":"","versions":[],"year":null,"flags":{"explicit":false,`+
		`"clean":false,"unknownArtist":false,"unknownTitle":false},"junk":[],"warnings":[]}` {
		t.Errorf("empty track: %s", empty)
	}
	if err := json.Unmarshal([]byte(`{"title":5}`), &back); err == nil {
		t.Error("a number title should not unmarshal")
	}
	if err := json.Unmarshal([]byte(`{"artists":[{"joiner":1}]}`), &back); err == nil {
		t.Error("a number joiner should not unmarshal")
	}
}

func TestTracklistJSON(t *testing.T) {
	first, last := 0.0, 130.5
	l := Tracklist{
		SpecVersion: SpecVersion,
		Entries: []Entry{{
			Line: 1, Kind: EntryKindTrack, Eol: EolCRLF, Raw: "00:00 A - B",
			Parts: Parts{Time: "00:00", Gap: " ", Rest: "A - B"},
			Time:  &TracklistTime{Raw: "00:00", Seconds: 0},
		}},
		Warnings: []TracklistWarning{{
			Code: WarningCodeTimeOutOfOrder, Line: 2, Severity: SeverityWarning,
			Suggestion: &Suggestion{Line: 1, Raw: "01:02:21", Seconds: 3741},
		}},
		Stats: Stats{Lines: 1, Tracks: 1, Timed: 1, FirstTime: &first, LastTime: &last},
	}
	b, _ := l.MarshalJSON()
	want := `{"specVersion":"` + SpecVersion + `","entries":[{"line":1,"kind":"track","eol":"\r\n",` +
		`"raw":"00:00 A - B","parts":{"lead":"","time":"00:00","gap":" ","rest":"A - B"},` +
		`"time":{"raw":"00:00","seconds":0},"track":null,"layered":false}],"warnings":[{` +
		`"code":"time-out-of-order","line":2,"severity":"warning","suggestion":{"line":1,` +
		`"raw":"01:02:21","seconds":3741}}],"stats":{"lines":1,"tracks":1,"markers":0,` +
		`"timed":1,"firstTime":0,"lastTime":130.5}}`
	if string(b) != want {
		t.Errorf("got\n%s\nwant\n%s", b, want)
	}
	line := 9
	r := EditResult{Text: "x", Warnings: []EditWarning{{Code: EditWarningCodeEditInvalid, Edit: 1, Line: &line}}}
	if b, _ := r.MarshalJSON(); string(b) != `{"text":"x","changed":[],"warnings":[{"code":"edit-invalid","edit":1,"line":9}]}` {
		t.Errorf("EditResult: %s", b)
	}
}

func TestEditJSON(t *testing.T) {
	for _, c := range []struct {
		e    Edit
		want string
	}{
		{SetTime(2, 90), `{"op":"set-time","line":2,"seconds":90}`},
		{Shift(-10.5), `{"op":"shift","seconds":-10.5}`},
		{ShiftFrom(30, 2), `{"op":"shift","seconds":30,"from":2}`},
		{ShiftLines(5, 1, 3), `{"op":"shift","seconds":5,"lines":[1,3]}`},
		{ShiftLines(5), `{"op":"shift","seconds":5,"lines":[]}`},
		{SortByTime(), `{"op":"sort-by-time"}`},
		{SetRest(1, "E - F"), `{"op":"set-rest","line":1,"rest":"E - F"}`},
		{Insert(0, "02:00 E - F"), `{"op":"insert","after":0,"text":"02:00 E - F"}`},
		{Delete(3), `{"op":"delete","line":3}`},
		{Edit{Op: "rename", Line: 1}, `{"op":"rename"}`},
	} {
		if b, _ := c.e.MarshalJSON(); string(b) != c.want {
			t.Errorf("got %s, want %s", b, c.want)
		}
	}
}
