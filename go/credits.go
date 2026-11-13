package trackparse

import "strings"

// R7.3 to R7.5: splitting a credit list into names. Port of js/src/credits.ts and
// rust/src/credits.rs.

// credit is an artist credit plus its absolute offset (UTF-16 units), used for ordering (R9.1).
type credit struct {
	name   string
	role   Role
	joiner *string
	source Source
	pos    int
	// list is the id of the credit list it came from (R7.6 joiner inheritance in R9.2).
	list int
}

// positionedYear is a year with its absolute offset (R9.6: the track year is the first in text
// order).
type positionedYear struct {
	year int
	pos  int
}

// listOptions are how one credit list is split.
type listOptions struct {
	role   Role
	source Source
	// lead is the joiner of the first name: nil, or the feat / producer marker as written.
	lead *string
	// splitWith is R7.3: with splits only in the main list of the artist side.
	splitWith bool
	// featMarkersJoin is R7.2: later feat markers act as joiners inside a featured list.
	featMarkersJoin bool
}

// listResult is a split credit list.
type listResult struct {
	credits []credit
	// unknown is R7.5: an unknown token was dropped.
	unknown bool
	// years are R7.5: dropped year-only names.
	years []positionedYear
}

// joinerHit is a joiner found in a list: its code-point span and its text as written.
type joinerHit struct {
	start int
	end   int
	raw   string
}

var andLike = []string{"&", "and", "+"}

// wordAt is the end of the word that starts at start.
func wordAt(text []rune, start int) int {
	end := start
	for end < len(text) && !isWhitespace(text[end]) {
		end++
	}
	return end
}

// nextWord is the word after a joiner; a tight joiner (, ;) ends it, since the name ends there.
func nextWord(text []rune, from int) string {
	a := from
	for a < len(text) && isWhitespace(text[a]) {
		a++
	}
	b := a
	for b < len(text) && !isWhitespace(text[b]) && text[b] != ',' && text[b] != ';' {
		b++
	}
	return string(sub(text, a, b))
}

// findJoiner is R7.3b: the leftmost joiner occurrence at or after from, honouring the guards.
func findJoiner(text []rune, from int, splitAtComma bool, opts *listOptions, c *ctx) (joinerHit, bool) {
	for p := max(from, 0); p < len(text); p++ {
		ch := text[p]
		if ch == ',' || ch == ';' {
			if ch == ',' && isDigit(before(text, p)) && isDigit(at(text, p+1)) {
				continue // 1,000
			}
			return joinerHit{start: p, end: p + 1, raw: string(ch)}, true
		}
		if p == 0 || before(text, p) != ' ' || isWhitespace(ch) {
			continue
		}
		end := wordAt(text, p)
		if end >= len(text) || text[end] != ' ' {
			continue // spaced joiners need a space after
		}
		word := string(sub(text, p, end))
		if isSpacedJoiner(word, text, end, splitAtComma, opts, c) {
			return joinerHit{start: p, end: end, raw: word}, true
		}
	}
	return joinerHit{}, false
}

func isSpacedJoiner(word string, text []rune, end int, splitAtComma bool, opts *listOptions, c *ctx) bool {
	if opts.featMarkersJoin {
		if _, ok := c.t.featMarkers.matchAt([]string{word}, 0); ok {
			return true
		}
	}
	for _, j := range c.t.spacedJoiners {
		matches := word == j.raw
		if !j.caseSensitive {
			matches = wordMatches(word, j.raw)
		}
		if !matches {
			continue
		}
		if j.raw == "with" && !opts.splitWith {
			return false
		}
		if j.isAnd {
			if c.splitAnd == splitNever {
				return false
			}
			if c.splitAnd == splitAuto && !splitAtComma {
				return false
			}
		}
		if containsString(andLike, j.raw) && inVocab(nextWord(text, end), c.t.noSplitBefore) {
			return false
		}
		return true
	}
	return false
}

// cleanName is R7.4: clean one raw name.
func cleanName(raw string) string {
	name := []rune(collapseSpaces(raw))
	if n := len(name); n >= 2 {
		if first := name[0]; (first == '"' || first == '\'') && name[n-1] == first {
			name = []rune(collapseRunes(name[1 : n-1]))
		}
	}
	s := string(name)
	for strings.HasSuffix(s, ",") || strings.HasSuffix(s, ";") {
		s = jsTrimEnd(s[:len(s)-1])
	}
	if space := strings.IndexByte(s, ' '); space > 0 && wordKey(s[:space]) == "by" {
		s = s[space+1:]
	}
	return collapseSpaces(s)
}

// rawName is a name before the R7.4 / R7.5 drops.
type rawName struct {
	name   string
	joiner *string
	pos    int
}

// unwrapList is R7.4: strip one pair of wrapping quotes from the whole list (no other such
// quote inside).
func unwrapList(list *skel) skel {
	t := trimSkel(list)
	n := len(t.text)
	if n < 2 {
		return t
	}
	q := t.text[0]
	if (q != '"' && q != '\'') || t.text[n-1] != q {
		return t
	}
	// indexOf(q, 1): the next such quote must be the last character.
	next := -1
	for i := 1; i < n; i++ {
		if t.text[i] == q {
			next = i
			break
		}
	}
	if next != n-1 {
		return t
	}
	inner := sliceSkel(&t, 1, n-1)
	return trimSkel(&inner)
}

// splitCredits is R7.3: split input into credits.
func splitCredits(input *skel, opts *listOptions, c *ctx) listResult {
	list := unwrapList(input) // R7.4: "Camo & Krooked"
	text := list.text
	var raw []rawName
	joiner := opts.lead
	splitAtComma := false
	start := 0
	for start <= len(text) {
		for start < len(text) && isWhitespace(text[start]) {
			start++
		}
		known := matchKnownAt(text, start, c, '-') // R7.3a
		hit, found := findJoiner(text, start+known, splitAtComma, opts, c)
		end := len(text)
		if found {
			end = hit.start
		}
		piece := sliceSkel(&list, start, end)
		pos := 0
		if start < len(list.pos) {
			pos = list.pos[start]
		} else if n := len(list.pos); n > 0 {
			pos = list.pos[n-1]
		}
		raw = append(raw, rawName{name: cleanName(renderSkel(&piece)), joiner: joiner, pos: pos})
		joiner = nil
		if !found {
			break
		}
		if hit.raw == "," || hit.raw == ";" {
			splitAtComma = true // R7.3: and after a tight joiner
		}
		start = hit.end
		j := hit.raw
		joiner = &j
	}
	return finishNames(raw, opts, c)
}

// finishNames is the R7.4 empty drops and the R7.5 year/unknown drops. R7.6: the first
// survivor takes the lead joiner (nil, or the feat/producer marker).
func finishNames(raw []rawName, opts *listOptions, c *ctx) listResult {
	named := make([]rawName, 0, len(raw))
	for _, r := range raw {
		if r.name != "" {
			named = append(named, r)
		}
	}
	count := len(named)
	unknown := false
	var years []positionedYear
	kept := make([]rawName, 0, len(named))
	for _, r := range named {
		if isUnknownToken(r.name, c.t) {
			unknown = true
			continue
		}
		if isAllDigits(r.name) && count > 1 {
			if year, ok := yearOf(r.name); ok {
				years = append(years, positionedYear{year: year, pos: r.pos})
				continue
			}
		}
		kept = append(kept, r)
	}
	list := c.listSeq
	c.listSeq++
	credits := make([]credit, len(kept))
	for i, r := range kept {
		j := r.joiner
		if i == 0 {
			j = opts.lead
		}
		credits[i] = credit{name: r.name, role: opts.role, joiner: j, source: opts.source, pos: r.pos, list: list}
	}
	return listResult{credits: credits, unknown: unknown, years: years}
}
