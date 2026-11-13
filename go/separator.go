package trackparse

// R6: artist/title separator, including the R6.2 suffix peeling shared with R8.2
// (js/src/separator.ts, rust/src/separator.rs).

// peeled is a dash suffix taken off by R6.2 or R8.2. cls is one of the peelable kinds: junk,
// flag, year or versions (Rust's Peelable).
type peeled struct {
	cls classification
	raw string
	pos int
}

// classifySegment is the R6.2 classification of a bare dash segment: placeholders make it
// Unknown.
func classifySegment(seg skel, c *ctx) classification {
	if hasGroup(&seg) {
		return classification{kind: classUnknown}
	}
	text := collapseRunes(seg.text)
	s := site{source: SourceTitle, delimiter: DelimiterDash, pos: startPos(&seg, 0)}
	return classify(text, s, c)
}

// isPeelable reports whether R6.2 / R8.2 can peel a classification.
func isPeelable(cls classification) bool {
	switch cls.kind {
	case classJunk, classFlag, classYear, classVersions:
		return true
	}
	return false
}

// peelSuffixes is R6.2 / R8.2: peel classifiable suffixes off the right end. relaxed drops
// conditions (i) to (iv) (R8.2 with an artist side). It gives the remaining segments and the
// peeled suffixes, left to right.
func peelSuffixes(segments []segment, relaxed bool, c *ctx) ([]segment, []peeled) {
	remaining := segments
	var out []peeled
	for len(remaining) >= 2 {
		last := remaining[len(remaining)-1]
		cls := classifySegment(last.skel, c)
		if !isPeelable(cls) {
			break
		}
		words := splitWordsRunes(last.skel.text)
		// (ii) does not apply to prefix-form versions ("Andy C b2b Hedex - Live at Printworks").
		prefixForm := false
		if cls.kind == classVersions {
			prefixForm = true
			for _, v := range cls.versions {
				if !v.prefixForm {
					prefixForm = false
					break
				}
			}
		}
		hasYear := false
		for _, w := range words {
			if _, ok := yearOf(w); ok {
				hasYear = true
				break
			}
		}
		ok := relaxed ||
			len(remaining) > 2 ||
			(len(words) >= 2 && !prefixForm) ||
			hasYear ||
			cls.kind == classJunk
		if !ok {
			break
		}
		out = append(out, peeled{cls: cls, raw: collapseRunes(last.skel.text), pos: startPos(&last.skel, 0)})
		remaining = remaining[:len(remaining)-1]
	}
	// Left to right.
	for i, j := 0, len(out)-1; i < j; i, j = i+1, j-1 {
		out[i], out[j] = out[j], out[i]
	}
	return remaining, out
}

// separatorResult is the outcome of R6.
type separatorResult struct {
	artist *skel
	title  skel
	// split: a separator split the string (R6.3 to R6.7), so R8.2 peels without conditions.
	split  bool
	peeled []peeled
}

// separatorInput is what R6 reads.
type separatorInput struct {
	skel skel
	mode Mode
	// uploader is the R6.8 fallback artist, or nil.
	uploader *skel
	// listContext is the R13.4.2 list context (internal, set only by the tracklist module): it
	// enables the R6.10 tight "letter-Uppercase" dash in tracklist mode.
	listContext bool
}

// splitParts is an artist side (nil when empty) and a title side.
type splitParts struct {
	artist *skel
	title  skel
}

// nonEmpty is the trimmed skeleton, or nil when it is empty.
func nonEmpty(s skel) *skel {
	t := trimSkel(&s)
	if t.isEmpty() {
		return nil
	}
	return &t
}

// splitAt cuts s around [cutStart, cutEnd).
func splitAt(s skel, cutStart, cutEnd int) splitParts {
	right := sliceFrom(&s, cutEnd)
	return splitParts{artist: nonEmpty(sliceSkel(&s, 0, cutStart)), title: trimSkel(&right)}
}

// resultOf is a separator result for split parts.
func resultOf(parts splitParts, p []peeled) separatorResult {
	return separatorResult{artist: parts.artist, title: parts.title, split: parts.artist != nil, peeled: p}
}

// firstDashSplit is R6.3: the first segment is the artist side, the rest (up to the last kept
// one) the title.
func firstDashSplit(sk skel, segments []segment) (splitParts, bool) {
	if len(segments) < 2 {
		return splitParts{}, false
	}
	first, second, last := segments[0], segments[1], segments[len(segments)-1]
	var artist *skel
	if !first.skel.isEmpty() {
		a := first.skel
		artist = &a
	}
	return splitParts{artist: artist, title: sliceSkel(&sk, second.start, last.end)}, true
}

// findSeparator is R6.
func findSeparator(in separatorInput, c *ctx) separatorResult {
	sk := in.skel
	mode := in.mode
	segments := splitAtSpacedDashes(&sk, true)
	if mode == ModeTracklist {
		return tracklistSeparator(sk, segments, in.listContext, c)
	}
	remaining, p := peelSuffixes(segments, false, c)

	// R6.3
	if parts, ok := firstDashSplit(sk, remaining); ok {
		return resultOf(parts, p)
	}

	rest := sliceSkel(&sk, 0, 0)
	if len(remaining) > 0 {
		rest = remaining[0].skel
	}
	if other, ok := splitWithoutSpacedDash(rest, mode, c); ok {
		return resultOf(other, p)
	}

	// R6.8: the uploader fallback does not warn noSeparator.
	var artist *skel
	if mode == ModeYouTube {
		artist = in.uploader
	}
	if !rest.isEmpty() && artist == nil {
		c.warn(WarningNoSeparator)
	}
	return separatorResult{artist: artist, title: rest, split: false, peeled: p}
}

// tracklistSeparator is R6.10, tracklist mode. The first spaced dash always separates (no R6.2
// peeling; R8.2 still peels later suffixes on the title side). Without one: R6.4, then the
// first "_-_", then (list context only) the last tight "letter-Uppercase" dash; both of these
// warn unspacedDashSplit.
func tracklistSeparator(sk skel, segments []segment, listContext bool, c *ctx) separatorResult {
	if parts, ok := firstDashSplit(sk, segments); ok {
		return resultOf(parts, nil)
	}
	rest := sliceSkel(&sk, 0, 0)
	if len(segments) > 0 {
		rest = segments[0].skel
	}
	if asym, ok := asymmetricDash(rest.text); ok {
		c.warn(WarningAsymmetricDashSplit)
		return resultOf(splitAt(rest, asym, asym+1), nil)
	}
	if u, ok := underscoreDash(rest.text); ok {
		c.warn(WarningUnspacedDashSplit)
		return resultOf(splitAt(rest, u, u+3), nil)
	}
	if listContext {
		if tight, ok := tightUpperDash(rest.text); ok {
			c.warn(WarningUnspacedDashSplit)
			return resultOf(splitAt(rest, tight, tight+1), nil)
		}
	}
	if !rest.isEmpty() {
		c.warn(WarningNoSeparator)
	}
	return separatorResult{title: rest}
}

// underscoreDash is the index of the first "_-_".
func underscoreDash(t []rune) (int, bool) {
	for i := 0; i+2 < len(t); i++ {
		if t[i] == '_' && t[i+1] == '-' && t[i+2] == '_' {
			return i, true
		}
	}
	return 0, false
}

// tightUpperDash is R6.10: the last "-" with a letter (R0.6) on its left and an uppercase
// letter (Lu) on its right, as in R6.7 ("Jay-Z-Numb" gives "Jay-Z" and "Numb").
func tightUpperDash(t []rune) (int, bool) {
	for i := len(t) - 2; i >= 1; i-- {
		if at(t, i) == '-' && isLetterBefore(t, i) && isUpperAt(t, i+1) {
			return i, true
		}
	}
	return 0, false
}

// splitWithoutSpacedDash is R6.4 to R6.7.
func splitWithoutSpacedDash(s skel, mode Mode, c *ctx) (splitParts, bool) {
	if asym, ok := asymmetricDash(s.text); ok {
		c.warn(WarningAsymmetricDashSplit)
		return splitAt(s, asym, asym+1), true
	}
	if mode == ModeYouTube {
		if quoted, ok := quotedTitle(s); ok {
			c.warn(WarningQuotedTitleSplit)
			return quoted, true
		}
		if by, ok := bySplit(s, c); ok {
			c.warn(WarningBySplit)
			lr := splitAt(s, by, by+4)
			var artist *skel
			if !lr.title.isEmpty() {
				right := lr.title
				artist = &right
			}
			title := s
			if lr.artist != nil {
				title = *lr.artist
			}
			return splitParts{artist: artist, title: title}, true
		}
	}
	if mode == ModeYouTube || mode == ModeFilename {
		if dash, ok := unspacedDash(s, c); ok {
			c.warn(WarningUnspacedDashSplit)
			return splitAt(s, dash, dash+1), true
		}
	}
	return splitParts{}, false
}

// asymmetricDash is R6.4: the first "-" with a space on exactly one side.
func asymmetricDash(t []rune) (int, bool) {
	for i := 1; i+1 < len(t); i++ {
		if at(t, i) == '-' && isWhitespace(before(t, i)) != isWhitespace(at(t, i+1)) {
			return i, true
		}
	}
	return 0, false
}

// quotedTitle is R6.5: Artist "Title" (extra).
func quotedTitle(s skel) (splitParts, bool) {
	t := s.text
	open := runeIndex(t, '"')
	if open <= 0 {
		return splitParts{}, false
	}
	rel := runeIndex(tail(t, open+1), '"')
	if rel < 0 {
		return splitParts{}, false
	}
	closeAt := rel + open + 1
	left := sliceSkel(&s, 0, open)
	artist := trimSkel(&left)
	if last := at(artist.text, artist.len()-1); last == ':' || last == '-' {
		shorter := sliceSkel(&artist, 0, artist.len()-1)
		artist = trimSkel(&shorter)
	}
	quoted, after := sliceSkel(&s, open+1, closeAt), sliceFrom(&s, closeAt+1)
	joined := concatSkel(&quoted, &after)
	title := trimSkel(&joined)
	var a *skel
	if !artist.isEmpty() {
		a = &artist
	}
	return splitParts{artist: a, title: title}, true
}

// bySplit is R6.6: the index of the space before the last usable " by ". The right side must
// be non-empty and not a single stopword ("Stand by Me").
func bySplit(s skel, c *ctx) (int, bool) {
	t := s.text
	// Candidates right to left; index 0 is never one (i > 0 in the JS loop).
	for i := len(t) - 4; i > 0; i-- {
		if at(t, i) != ' ' || at(t, i+1) != 'b' || at(t, i+2) != 'y' || at(t, i+3) != ' ' {
			continue
		}
		// Placeholder-only words (groups such as "(Official Video)") don't count here.
		var right []string
		for _, w := range splitWordsRunes(tail(t, i+4)) {
			if w == string(ph) {
				continue
			}
			right = append(right, w)
		}
		singleStopword := len(right) == 1 && inVocab(right[0], c.t.stopwords)
		if len(right) > 0 && !singleStopword {
			return i, true
		}
	}
	return 0, false
}

// unspacedDash is R6.7: the chosen unspaced dash index ("Jay-Z-Numb").
func unspacedDash(s skel, c *ctx) (int, bool) {
	t := s.text
	last, found := 0, false
	for i := 1; i+1 < len(t); i++ {
		if at(t, i) != '-' || isWhitespace(before(t, i)) || isWhitespace(at(t, i+1)) {
			continue
		}
		if !(at(t, i+1) == '"' || isLetterAt(t, i+1)) {
			continue
		}
		if len(c.knownKeys) > 0 && isKnownArtist(renderPrefix(&s, i), c) {
			return i, true
		}
		last, found = i, true
	}
	return last, found
}

// renderPrefix is the rendered text of s[:end].
func renderPrefix(s *skel, end int) string {
	head := sliceSkel(s, 0, end)
	return renderSkel(&head)
}
