package trackparse

import (
	"cmp"
	"slices"
)

// R7: the artist side. Port of js/src/artists.ts and rust/src/artists.rs.

// artistSideResult is what R7 found on the artist side.
type artistSideResult struct {
	credits       []credit
	junk          []positionedJunk
	years         []positionedYear
	unknownArtist bool
	// explicit and clean are R7.1: artist-side Flag groups.
	explicit bool
	clean    bool
}

// featMarker is an unbracketed feat marker: its code-point span and its text as written.
type featMarker struct {
	start  int
	end    int
	marker string
}

// findFeatMarker is R7.2: the leftmost unbracketed feat marker at or after from, with a space
// (or placeholder) on its left and a space on its right.
func findFeatMarker(s *skel, c *ctx, from int) (featMarker, bool) {
	spans := wordSpans(s.text)
	words := spanTexts(spans)
	for i, span := range spans {
		if span.start < from {
			continue
		}
		if b := before(s.text, span.start); b != ' ' && b != ph {
			continue
		}
		m, ok := c.t.featMarkers.matchAt(words, i)
		if !ok {
			continue
		}
		lastIndex := i + m.length - 1
		if lastIndex < 0 || lastIndex >= len(spans) {
			continue
		}
		last := spans[lastIndex]
		if at(s.text, last.end) != ' ' {
			continue
		}
		return featMarker{
			start:  span.start,
			end:    last.end,
			marker: string(sub(s.text, span.start, last.end)),
		}, true
	}
	return featMarker{}, false
}

// parseArtistSide is R7.1 to R7.5 over an artist-side skeleton.
func parseArtistSide(side *skel, c *ctx) artistSideResult {
	var result artistSideResult
	removed := map[int]struct{}{}

	// R7.1
	for _, ig := range groupsOf(side) {
		g := ig.group
		cl := classify(g.inner, site{source: SourceArtist, delimiter: g.open, pos: g.pos}, c)
		switch cl.kind {
		case classFeat, classProducer:
			result.credits = append(result.credits, cl.list.credits...)
			result.years = append(result.years, cl.list.years...)
			if cl.list.unknown {
				result.unknownArtist = true
			}
		case classFlag:
			if cl.flag == flagExplicit {
				result.explicit = true
			} else {
				result.clean = true
			}
		case classYear:
			result.years = append(result.years, positionedYear{year: cl.year, pos: g.pos})
		case classJunk:
			result.junk = append(result.junk, positionedJunk{raw: g.inner, kind: cl.junk, pos: g.pos})
		default:
			// Versions and unknown groups stay in the names.
			continue
		}
		removed[g.pos] = struct{}{}
	}
	text := removeGroups(side, removed)

	// R7.2
	feat, hasFeat := findFeatMarker(&text, c, 0)
	var main skel
	if hasFeat {
		head := sliceSkel(&text, 0, feat.start)
		main = trimSkel(&head)
	} else {
		main = trimSkel(&text)
	}
	mainList := splitCredits(&main, &listOptions{
		role:      RolePrimary,
		source:    SourceArtist,
		splitWith: true,
	}, c)
	result.credits = append(result.credits, mainList.credits...)
	result.years = append(result.years, mainList.years...)
	if mainList.unknown {
		result.unknownArtist = true
	}
	if hasFeat {
		rest := sliceFrom(&text, feat.end)
		trimmed := trimSkel(&rest)
		marker := feat.marker
		featured := splitCredits(&trimmed, &listOptions{
			role:            RoleFeatured,
			source:          SourceArtist,
			lead:            &marker,
			featMarkersJoin: true,
		}, c)
		result.credits = append(result.credits, featured.credits...)
		result.years = append(result.years, featured.years...)
		if featured.unknown {
			result.unknownArtist = true
		}
	}
	// Stable, like the JS sort.
	slices.SortStableFunc(result.credits, func(a, b credit) int { return cmp.Compare(a.pos, b.pos) })
	return result
}
