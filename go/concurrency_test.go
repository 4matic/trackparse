package trackparse

// Concurrent use: a Parser with options, the zero Parser, the package functions,
// ParseTracklistWith and ApplyEdits, called from 16 goroutines at once, must give the same
// bytes as one goroutine calling them in turn. CI runs it under -race (go test -race ./...),
// which also reports any write to shared state.

import (
	"strings"
	"sync"
	"testing"
)

// concurrencyInputs is single-track inputs: fixture inputs when ../spec is there, plus a few
// written here so the test means something in the module zip too.
func concurrencyInputs(limit int) []string {
	out := []string{
		"Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)",
		"01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
		"Noisia - Stigma (VIP",
		"\"Numb\" by Linkin Park",
		"07_Burial_-_Archangel.mp3",
		"Sub Focus, Wilkinson & Dimension feat. Kojo - Out Now (Extended Mix) [HD]",
	}
	for _, c := range fixtureCases() {
		if len(out) >= limit {
			break
		}
		if c.fn == "parse" || c.fn == "parseArtists" {
			out = append(out, c.input)
		}
	}
	return out
}

func TestConcurrentUse(t *testing.T) {
	limit := 2000
	if raceEnabled {
		limit = 300
	}
	inputs := concurrencyInputs(limit)
	parser := NewParser(ParseOptions{
		KnownArtists: []string{"Chase & Status", "Camo & Krooked", "Above & Beyond"},
		SplitAnd:     SplitAndAlways,
		Keywords: Keywords{
			VersionHeads: []VersionHead{{Key: "rerub", Type: VersionTypeRework}},
			Junk:         []JunkPhrase{{Phrase: "out now", Kind: JunkKindPromo}},
			FeatMarkers:  []string{"w/"},
		},
	})
	var zero Parser
	listOptions := TracklistOptions{
		KnownArtists: []string{"Chase & Status"},
		AudioSeconds: Ptr(3600.0),
		MarkerWords:  []string{"drop"},
	}
	text := strings.Join([]string{
		"Hour 2",
		"48:20 Noisia - Stigma",
		"52:47 Mefjus & Kasra - Bandwagon (Phace Remix)",
		"03:00 Drop",
		"59:31 Alix Perez - Fade Away",
		"01:07:44 Chase & Status - Baddadan",
		"00:11:02 Current Value feat. Teddy Killerz - Payload",
		"1:02:03 Hybris - Spinning Top",
	}, "\r\n")
	edits := []Edit{SetTime(7, 4262), ShiftFrom(45, 2), SortByTime(), Insert(0, "00:00 Intro"), Delete(4)}

	// One run of every call for input i, as JSON.
	run := func(i int) []string {
		s := inputs[i]
		a, b, c := parser.Parse(s), zero.Parse(s), Parse(s)
		out := []string{
			string(marshal(&a)), string(marshal(&b)), string(marshal(&c)),
			string(marshal(artistList(parser.ParseArtists(s)))),
			FormatWith(a, FormatOptions{Feat: FeatTitle, FeatMarker: Ptr("ft.")}),
		}
		if i%10 == 0 {
			list := ParseTracklistWith(text+"\r\n"+s, listOptions)
			res := ApplyEdits(text+"\n"+s, edits)
			out = append(out, string(marshal(&list)), string(marshal(&res)))
		}
		return out
	}
	want := make([][]string, len(inputs))
	for i := range inputs {
		want[i] = run(i)
	}

	const workers = 16
	var wg sync.WaitGroup
	for g := 0; g < workers; g++ {
		wg.Add(1)
		go func(g int) {
			defer wg.Done()
			// Each goroutine starts at its own offset, so different inputs run side by side.
			for k := range inputs {
				i := (k + g*len(inputs)/workers) % len(inputs)
				got := run(i)
				for j := range got {
					if got[j] != want[i][j] {
						t.Errorf("goroutine %d, input %q, call %d: got\n%s\nwant\n%s", g, inputs[i], j, got[j], want[i][j])
						return
					}
				}
			}
		}(g)
	}
	wg.Wait()
}
