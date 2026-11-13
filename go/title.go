package trackparse

import "strings"

// R8: the title side (js/src/title.ts, rust/src/title.rs).

// extracted is everything the title side (and R6.2 peeling) can extract.
type extracted struct {
	credits       []credit
	versions      []versionDraft
	junk          []positionedJunk
	years         []positionedYear
	explicit      bool
	clean         bool
	unknownArtist bool
}

// setFlag records an Explicit or Clean flag.
func (out *extracted) setFlag(f flag) {
	switch f {
	case flagExplicit:
		out.explicit = true
	case flagClean:
		out.clean = true
	}
}

// addPeeled records R6.2 / R8.2 peeled dash suffixes.
func addPeeled(out *extracted, list []peeled) {
	for _, p := range list {
		switch p.cls.kind {
		case classVersions:
			out.versions = append(out.versions, p.cls.versions...)
		case classJunk:
			out.junk = append(out.junk, positionedJunk{raw: p.raw, kind: p.cls.junk, pos: p.pos})
		case classFlag:
			out.setFlag(p.cls.flag)
		case classYear:
			out.years = append(out.years, positionedYear{year: p.cls.year, pos: p.pos})
		}
	}
}

// titleSideResult is the title and whether it is an unknown placeholder (R8.6).
type titleSideResult struct {
	title        string
	unknownTitle bool
}

// parseTitleSide is R8. relaxed: an artist side was split off (R8.2 peels without conditions).
func parseTitleSide(input skel, relaxed, youtube bool, out *extracted, c *ctx) titleSideResult {
	removed := extractGroups(input, out, c) // R8.1

	// R8.2
	segments := splitAtSpacedDashes(input, false)
	remaining, p := peelSuffixes(segments, relaxed, c)
	addPeeled(out, p)
	lastKeptEnd := 0
	if len(remaining) > 0 {
		lastKeptEnd = remaining[len(remaining)-1].end
	}
	sk := sliceSkel(input, 0, lastKeptEnd)

	sk = extractUnbracketedCredits(sk, out, c) // R8.3
	sk = removeGroups(sk, removed)
	if youtube {
		sk = stripTrailingJunk(sk, out, c) // R8.4
	}

	title := cleanupTitle(renderSkel(sk))                     // R8.5
	unknownTitle := title != "" && isUnknownToken(title, c.t) // R8.6
	return titleSideResult{title: title, unknownTitle: unknownTitle}
}

// extractGroups is R8.1: classify every group. It gives the offsets of the groups to remove.
func extractGroups(sk skel, out *extracted, c *ctx) map[int]bool {
	removed := map[int]bool{}
	for _, ig := range groupsOf(sk) {
		g := ig.g
		cls := classify(g.inner, site{source: SourceTitle, delimiter: g.open, pos: g.pos}, c)
		switch cls.kind {
		case classVersions:
			out.versions = append(out.versions, cls.versions...)
		case classJunk:
			out.junk = append(out.junk, positionedJunk{raw: g.inner, kind: cls.junk, pos: g.pos})
		case classFlag:
			out.setFlag(cls.flag)
		case classYear:
			out.years = append(out.years, positionedYear{year: cls.year, pos: g.pos})
		case classFeat, classProducer:
			out.credits = append(out.credits, cls.list.credits...)
			out.years = append(out.years, cls.list.years...)
			if cls.list.unknown {
				out.unknownArtist = true
			}
		default:
			continue
		}
		removed[g.pos] = true
	}
	return removed
}

// markerHit is an R8.3 marker in the title.
type markerHit struct {
	start  int
	end    int
	marker string
	role   Role
}

// titleMarkers are the R8.3 markers, left to right, each with a space on both sides: feat
// markers from titleFeatMarkers ("feat.", "ft.", "featuring"; undotted feat and ft stay text)
// and producer markers from titleProducerMarkers ("prod.", "prod. by", "produced by",
// "prod.by").
func titleMarkers(sk skel, c *ctx) []markerHit {
	spans := wordSpans(sk.text)
	words := spanTexts(spans)
	var hits []markerHit
	for i := 0; i < len(spans); {
		span := spans[i]
		step := i + 1
		if before(sk.text, span.start) != ' ' {
			i = step
			continue
		}
		var role Role
		lastIndex := 0
		if feat, ok := c.t.titleFeatMarkers.matchAt(words, i); ok {
			role, lastIndex = RoleFeatured, i+feat.length-1
		} else if prod, ok := c.t.titleProducerMarkers.matchAt(words, i); ok {
			role, lastIndex = RoleProducer, i+prod.length-1
		} else {
			i = step
			continue
		}
		if lastIndex < 0 || lastIndex >= len(spans) {
			i = step
			continue
		}
		last := spans[lastIndex]
		if at(sk.text, last.end) != ' ' {
			i = step
			continue
		}
		hits = append(hits, markerHit{
			start:  span.start,
			end:    last.end,
			marker: string(sub(sk.text, span.start, last.end)),
			role:   role,
		})
		i = lastIndex + 1
	}
	return hits
}

// extractUnbracketedCredits is R8.3: "Title feat. X" and "Title prod. by Y" become credits,
// removed from the title. A list runs to the next placeholder, the next marker of the other
// kind, or the end; later feat markers inside a featured list are joiners (R7.2).
func extractUnbracketedCredits(input skel, out *extracted, c *ctx) skel {
	markers := titleMarkers(input, c)
	if len(markers) == 0 {
		return input
	}
	// indexOf(PH, from) for every from, computed once so many markers stay linear (R0.8).
	n := len(input.text)
	nextGroup := make([]int, n+1)
	nextGroup[n] = n
	for k := n - 1; k >= 0; k-- {
		if input.text[k] == ph {
			nextGroup[k] = k
		} else {
			nextGroup[k] = nextGroup[k+1]
		}
	}
	nextGroupAt := func(k int) int {
		if k >= 0 && k <= n {
			return nextGroup[k]
		}
		return n
	}
	type cut struct{ start, end int }
	var cuts []cut
	for i := 0; i < len(markers); {
		hit := markers[i]
		listEnd := nextGroupAt(hit.end)
		for _, m := range markers[i+1:] {
			if m.role != hit.role && m.start >= hit.end {
				if m.start < listEnd {
					listEnd = m.start
				}
				break
			}
		}
		list := trimSkel(sliceSkel(input, hit.end, listEnd))
		if !list.isEmpty() {
			marker := hit.marker
			r := splitCredits(list, listOptions{
				role:            hit.role,
				source:          SourceTitle,
				lead:            &marker,
				splitWith:       false,
				featMarkersJoin: hit.role == RoleFeatured,
			}, c)
			out.credits = append(out.credits, r.credits...)
			out.years = append(out.years, r.years...)
			if r.unknown {
				out.unknownArtist = true
			}
			cuts = append(cuts, cut{hit.start, listEnd})
		} else {
			listEnd = hit.end
		}
		for i < len(markers) && markers[i].start < listEnd {
			i++
		}
	}
	// The cuts are disjoint and ascending: remove them in one pass.
	text := make([]rune, 0, n)
	pos := make([]int, 0, n)
	from := 0
	for _, k := range cuts {
		start := max(min(k.start, n), from)
		text = append(text, clamped(input.text, from, start)...)
		pos = append(pos, clamped(input.pos, from, start)...)
		from = max(min(k.end, n), from)
	}
	text = append(text, clamped(input.text, from, n)...)
	pos = append(pos, clamped(input.pos, from, len(input.pos))...)
	return skel{text: text, pos: pos, groups: input.groups}
}

// stripTrailingJunk is R8.4: strip trailing unbracketed junk phrases (optionally after a lone
// "-" or "|"). Word spans are computed once and walked backwards, so repeated junk stays linear
// (R0.8).
func stripTrailingJunk(input skel, out *extracted, c *ctx) skel {
	sk := trimSkel(input)
	spans := wordSpans(sk.text)
	words := spanTexts(spans)
	end := len(spans) // words still in the title
	for {
		m, ok := c.t.junkPhrases.matchEnding(words, end)
		if !ok {
			break
		}
		first := end - min(m.length, end)
		keep := first
		if keep > 0 && isLoneEdge(words[keep-1]) {
			keep--
		}
		if keep == 0 {
			break // never strip the whole title
		}
		from := 0
		if first < len(spans) {
			from = spans[first].start
		}
		to := len(sk.text)
		if end >= 1 && end-1 < len(spans) {
			to = spans[end-1].end
		}
		p := 0
		if from < len(sk.pos) {
			p = sk.pos[from]
		}
		out.junk = append(out.junk, positionedJunk{
			raw:  collapseRunes(sub(sk.text, from, to)),
			kind: m.value,
			pos:  p,
		})
		end = keep
	}
	if end == len(spans) {
		return sk
	}
	cutAt := 0
	if end >= 1 && end-1 < len(spans) {
		cutAt = spans[end-1].end
	}
	return trimSkel(sliceSkel(sk, 0, cutAt))
}

// isLoneEdge reports whether a word is a lone "-" or "|".
func isLoneEdge(word string) bool {
	return word == "-" || word == "|"
}

// stripLoneEdges removes lone "-" and "|" words at both ends of a space-separated string.
func stripLoneEdges(s string) string {
	words := strings.Split(s, " ")
	a, b := 0, len(words)
	for a < b && isLoneEdge(words[a]) {
		a++
	}
	for b > a && isLoneEdge(words[b-1]) {
		b--
	}
	return strings.Join(words[a:b], " ")
}

// cleanupTitle is R8.5.
func cleanupTitle(raw string) string {
	title := stripLoneEdges(collapseSpaces(raw))
	t := []rune(title)
	if len(t) >= 2 {
		q, last := t[0], t[len(t)-1]
		if (q == '"' || q == '\'') && last == q {
			return stripLoneEdges(collapseRunes(sub(t, 1, len(t)-1)))
		}
	}
	return title
}
