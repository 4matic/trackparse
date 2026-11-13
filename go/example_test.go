package trackparse_test

import (
	"bytes"
	"encoding/json"
	"fmt"
	"strings"

	trackparse "github.com/4matic/trackparse/go"
)

// Parse a messy track string: every part it recognizes goes into a field, and anything it does
// not recognize stays in the title.
func Example() {
	track := trackparse.Parse(
		"01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
	)

	for _, a := range track.Artists {
		fmt.Printf("%s (%s)\n", a.Name, a.Role)
	}
	fmt.Println(track.Title)
	fmt.Println(track.FullTitle)
	v := track.Versions[0]
	fmt.Println(v.Type, v.Artists[0].Name, v.Modifiers)
	fmt.Println(track.Junk[0].Raw, track.Junk[0].Kind)
	fmt.Println(track.Position.Number, track.Mode)

	fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
		Joiners: trackparse.JoinersCanonical,
	}))
	// Output:
	// Wilkinson (primary)
	// Becky Hill (featured)
	// Tom Cane (featured)
	// Afterglow
	// Afterglow (Sub Focus VIP Remix)
	// remix Sub Focus [vip]
	// Official Video video
	// 1 youtube
	// Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)
}

func ExampleParse() {
	track := trackparse.Parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)")

	v := track.Versions[0]
	fmt.Println(track.Artists[0].Name, "/", track.Title)
	fmt.Println(v.Type, v.Raw, v.Delimiter)
	fmt.Println(*v.Descriptor, *v.Year)
	fmt.Println(track.Year == nil, len(track.Warnings))
	// Output:
	// Queen / Bohemian Rhapsody
	// live Live at Wembley 1986 (
	// at Wembley 1986
	// true 0
}

func ExampleParseWith() {
	names := func(t trackparse.Track) []string {
		var out []string
		for _, a := range t.Artists {
			out = append(out, a.Name)
		}
		return out
	}

	fmt.Println(names(trackparse.Parse("Chase & Status - Baddadan")))
	fmt.Println(names(trackparse.ParseWith("Chase & Status - Baddadan", trackparse.ParseOptions{
		KnownArtists: []string{"Chase & Status"},
	})))

	track := trackparse.ParseWith(`"Numb" by Linkin Park`, trackparse.ParseOptions{
		Mode: trackparse.ModeYouTube,
	})
	fmt.Println(names(track), track.Title, track.Warnings)

	track = trackparse.ParseWith("Shelter", trackparse.ParseOptions{
		Mode:     trackparse.ModeYouTube,
		Uploader: "Porter Robinson",
	})
	fmt.Println(names(track), track.Title)
	// Output:
	// [Chase Status]
	// [Chase & Status]
	// [Linkin Park] Numb [bySplit]
	// [Porter Robinson] Shelter
}

// Keywords add words to the built-in lists.
func ExampleParseWith_keywords() {
	fmt.Println(trackparse.Parse("Artist - Song [Some Label Rip]").Title)

	track := trackparse.ParseWith("Artist - Song [Some Label Rip]", trackparse.ParseOptions{
		Keywords: trackparse.Keywords{
			Junk: []trackparse.JunkPhrase{{Phrase: "some label rip", Kind: trackparse.JunkKindLabel}},
		},
	})
	fmt.Println(track.Title)
	fmt.Println(track.Junk[0].Raw, track.Junk[0].Kind)
	// Output:
	// Song [Some Label Rip]
	// Song
	// Some Label Rip label
}

func ExampleParseArtists() {
	for _, a := range trackparse.ParseArtists("Sub Focus, Wilkinson & Dimension feat. Kojo") {
		joiner := "<nil>"
		if a.Joiner != nil {
			joiner = *a.Joiner
		}
		fmt.Printf("%s:%s joiner=%s\n", a.Name, a.Role, joiner)
	}
	// Output:
	// Sub Focus:primary joiner=<nil>
	// Wilkinson:primary joiner=,
	// Dimension:primary joiner=&
	// Kojo:featured joiner=feat.
}

func ExampleFormatWith() {
	track := trackparse.Parse("01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)")

	fmt.Println(trackparse.Format(track))
	fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
		Feat:    trackparse.FeatTitle,
		Joiners: trackparse.JoinersCanonical,
	}))
	fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{
		Feat:     trackparse.FeatOmit,
		Versions: trackparse.VersionsNone,
	}))
	fmt.Println(trackparse.FormatWith(track, trackparse.FormatOptions{Position: true}))
	// Output:
	// Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)
	// Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)
	// Wilkinson - Afterglow
	// 01. Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)
}

func ExampleNormalize() {
	fmt.Println(trackparse.Normalize("Noisia​ – Stigma （VIP）"))
	// Output:
	// Noisia - Stigma (VIP)
}

func ExampleAllArtists() {
	track := trackparse.Parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)")
	for _, a := range trackparse.AllArtists(track) {
		fmt.Printf("%s:%s\n", a.Name, a.Role)
	}
	// Output:
	// Wilkinson:primary
	// Becky Hill:featured
	// Sub Focus:remixer
}

func ExampleParseTracklist() {
	text := strings.Join([]string{
		"Hour 2",
		"48:20 Noisia - Stigma",
		"52:47 Mefjus & Kasra - Bandwagon (Phace Remix)",
		"56:05 Black Sun Empire - ID",
		"59:31 Alix Perez - Fade Away",
		"01:03:10 Interview with Hybris",
		"01:07:44 Imanu - Sacrifice",
		"00:11:02 Current Value feat. Teddy Killerz - Payload", // typo for 01:11:02
		"01:15:36 Hybris - Spinning Top",
	}, "\r\n")

	list := trackparse.ParseTracklist(text)

	var joined strings.Builder
	var kinds []trackparse.EntryKind
	for _, e := range list.Entries {
		joined.WriteString(e.Raw + string(e.Eol))
		kinds = append(kinds, e.Kind)
	}
	fmt.Println(joined.String() == text)
	fmt.Println(kinds)

	e := list.Entries[2]
	fmt.Println(e.Time.Raw, e.Time.Seconds)
	fmt.Printf("%q\n", e.Parts.Rest)
	fmt.Println(len(e.Track.Artists), e.Track.Title, e.Track.Versions[0].Type)
	fmt.Println(list.Entries[3].Track.Flags.UnknownTitle)

	for _, w := range list.Warnings {
		fmt.Println(w.Code, w.Line, w.Severity)
		if s := w.Suggestion; s != nil {
			fmt.Println("  suggestion:", s.Line, s.Raw, s.Seconds)
		}
	}
	s := list.Stats
	fmt.Println(s.Lines, s.Tracks, s.Markers, s.Timed, *s.FirstTime, *s.LastTime)
	// Output:
	// true
	// [header track track track track marker track track track]
	// 52:47 3167
	// "Mefjus & Kasra - Bandwagon (Phace Remix)"
	// 2 Bandwagon remix
	// true
	// no-start-at-zero 2 info
	// time-out-of-order 8 warning
	//   suggestion: 8 01:11:02 4262
	// 9 7 1 8 2900 4536
}

func ExampleParseTracklistWith() {
	list := trackparse.ParseTracklistWith("00:00 A - B\n1:02:03 C - D", trackparse.TracklistOptions{
		AudioSeconds: 3600,
	})
	fmt.Println(list.Warnings[0].Code, list.Warnings[0].Line)
	// Output:
	// time-after-audio-end 2
}

func ExampleApplyEdits() {
	text := "48:20 Noisia - Stigma\r\n59:31 Alix Perez - Fade Away\r\n00:03:10 Imanu - Sacrifice\r\n"

	result := trackparse.ApplyEdits(text, []trackparse.Edit{
		trackparse.SetTime(3, 3790),
		trackparse.ShiftFrom(45, 2),
	})
	fmt.Printf("%q\n", result.Text)
	fmt.Println(result.Changed)
	w := result.Warnings[0]
	fmt.Println(w.Code, w.Edit, *w.Line)

	result = trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.ShiftLines(-3000, 1)})
	fmt.Println(result.Warnings[0].Code)

	result = trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.Delete(9)})
	fmt.Println(result.Warnings[0].Code, result.Text == text)
	// Output:
	// "48:20 Noisia - Stigma\r\n01:00:16 Alix Perez - Fade Away\r\n01:03:55 Imanu - Sacrifice\r\n"
	// [2 3]
	// style-upgraded 1 2
	// time-clamped
	// edit-invalid true
}

func ExampleParser() {
	parser := trackparse.NewParser(trackparse.ParseOptions{
		KnownArtists: []string{"Chase & Status", "Camo & Krooked"},
		Keywords: trackparse.Keywords{
			VersionHeads: []trackparse.VersionHead{{Key: "rerub", Type: trackparse.VersionTypeRework}},
		},
	})

	track := parser.Parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)")
	fmt.Println(len(track.Artists), track.Artists[0].Name, track.Artists[1].Name)
	fmt.Println(track.Versions[0].Type)

	// The zero Parser uses the default options.
	var plain trackparse.Parser
	fmt.Println(len(plain.Parse("Chase & Status - Baddadan").Artists))
	// Output:
	// 2 Chase & Status Camo & Krooked
	// rework
	// 2
}

func ExampleTrack_MarshalJSON() {
	track := trackparse.Parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]")
	b, err := json.Marshal(track)
	if err != nil {
		panic(err)
	}
	fmt.Println(string(b))

	var back trackparse.Track
	if err := json.Unmarshal(b, &back); err != nil {
		panic(err)
	}
	fmt.Println(back.FullTitle, back.Versions[0].Artists[0].Name)
	// Output:
	// {"input":"Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]","mode":"youtube","position":null,"timestamp":null,"artists":[{"name":"Wilkinson","role":"primary","joiner":null,"source":"artist"},{"name":"Becky Hill","role":"featured","joiner":"ft.","source":"artist"}],"title":"Afterglow","fullTitle":"Afterglow (Sub Focus VIP Remix)","versions":[{"type":"remix","raw":"Sub Focus VIP Remix","artists":[{"name":"Sub Focus","role":"remixer","joiner":null,"source":"version"}],"modifiers":["vip"],"descriptor":null,"year":null,"unknownArtist":false,"delimiter":"("}],"year":null,"flags":{"explicit":false,"clean":false,"unknownArtist":false,"unknownTitle":false},"junk":[{"raw":"HD","kind":"quality"}],"warnings":[]}
	// Afterglow (Sub Focus VIP Remix) Sub Focus
}

// encoding/json escapes <, > and & (and U+2028, U+2029) in what MarshalJSON returns. An
// Encoder with SetEscapeHTML(false) keeps the bytes JSON.stringify writes.
func ExampleTrack_MarshalJSON_escapeHTML() {
	track := trackparse.Parse("Chase & Status - Baddadan")

	b, _ := json.Marshal(track.Artists[1])
	fmt.Println(string(b))

	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	_ = enc.Encode(track.Artists[1])
	fmt.Print(buf.String())

	b, _ = track.Artists[1].MarshalJSON()
	fmt.Println(string(b))
	// Output:
	// {"name":"Status","role":"primary","joiner":"\u0026","source":"artist"}
	// {"name":"Status","role":"primary","joiner":"&","source":"artist"}
	// {"name":"Status","role":"primary","joiner":"&","source":"artist"}
}
