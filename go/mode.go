package trackparse

// R2: mode resolution, platform suffixes, filename handling and youtube pipes (js/src/mode.ts,
// rust/src/mode.rs).

// positionedJunk is a junk fragment with its absolute offset (R9 orders junk by it).
type positionedJunk struct {
	raw string
	// kind is a junk kind name; keywords.junk values pass through as given.
	kind string
	pos  int
}

// piece is a contiguous piece of the normalized string and its absolute offset.
type piece struct {
	text []rune
	base int
}

// preludeResult is the outcome of R2.
type preludeResult struct {
	mode Mode
	// working is the working string (R2.4 step 2 or 4), or the artist piece in the
	// "artist | title" case.
	working piece
	// pipeTitle is the R2.4 step 3 title piece when two dash-less pipe segments remain.
	pipeTitle *piece
	junk      []positionedJunk
}

// asciiLowerRunes lowercases A-Z only.
func asciiLowerRunes(t []rune) []rune {
	out := make([]rune, len(t))
	for i, r := range t {
		if r >= 'A' && r <= 'Z' {
			r += 'a' - 'A'
		}
		out[i] = r
	}
	return out
}

// hasRuneSuffix reports whether t ends with suffix.
func hasRuneSuffix(t, suffix []rune) bool {
	if len(suffix) > len(t) {
		return false
	}
	off := len(t) - len(suffix)
	for i, r := range suffix {
		if t[off+i] != r {
			return false
		}
	}
	return true
}

// runeIndex is the first index of r in t, or -1.
func runeIndex(t []rune, r rune) int {
	for i, x := range t {
		if x == r {
			return i
		}
	}
	return -1
}

// lastRuneIndex is the last index of r in t, or -1.
func lastRuneIndex(t []rune, r rune) int {
	for i := len(t) - 1; i >= 0; i-- {
		if t[i] == r {
			return i
		}
	}
	return -1
}

// stripPlatformSuffixes is R2.2: strip trailing platform suffixes (longest first, repeatedly).
func stripPlatformSuffixes(s []rune, c *ctx, junk *[]positionedJunk) []rune {
	text := append([]rune(nil), s...)
	for stripped := true; stripped; {
		stripped = false
		lower := asciiLowerRunes(text)
		for _, suffix := range c.t.platformSuffixes {
			suffixLower := []rune(asciiLower(suffix))
			if !hasRuneSuffix(lower, suffixLower) {
				continue
			}
			start := len(text) - len(suffixLower)
			*junk = append(*junk, positionedJunk{
				raw:  string(tail(text, start+3)),
				kind: "platform",
				pos:  utf16Len(sub(text, 0, start)) + 3,
			})
			text = sub(text, 0, start)
			stripped = true
			break
		}
	}
	return text
}

// hasExtension reports whether s ends in a known file extension (R2.3).
func hasExtension(s []rune, c *ctx) bool {
	dot := lastRuneIndex(s, '.')
	if dot < 0 {
		return false
	}
	return c.t.extensions.has(asciiLower(string(tail(s, dot+1))))
}

// splitPipes splits a skeleton at top-level " | " and " || " separators into trimmed segments
// (R2.4).
func splitPipes(sk skel) []segment {
	t := sk.text
	type cut struct{ a, b int }
	var cuts []cut
	for i := 1; i < len(t); i++ {
		if before(t, i) == ' ' && at(t, i) == '|' {
			bars := 1
			if at(t, i+1) == '|' {
				bars = 2
			}
			if at(t, i+bars) == ' ' {
				cuts = append(cuts, cut{i - 1, i + bars + 1})
				i += bars
			}
		}
	}
	cuts = append(cuts, cut{len(t), len(t)})
	segments := make([]segment, 0, len(cuts))
	start := 0
	for _, k := range cuts {
		s, e := start, k.a
		for s < e && at(t, s) == ' ' {
			s++
		}
		for e > s && before(t, e) == ' ' {
			e--
		}
		segments = append(segments, segment{skel: sliceSkel(sk, s, e), start: s, end: e})
		start = k.b
	}
	return segments
}

// trailingJunkPhrase is the R8.4 test: the text ends with an unbracketed junk phrase (genres
// excluded). It gives the code-point index where the phrase starts and its kind.
func trailingJunkPhrase(text []rune, c *ctx) (int, string, bool) {
	spans := wordSpans(text)
	words := spanTexts(spans)
	m, ok := c.t.junkPhrases.matchEnding(words, len(spans))
	if !ok {
		return 0, "", false
	}
	first := len(spans) - m.length
	if first < 0 || first >= len(spans) {
		return 0, "", false
	}
	return spans[first].start, m.value, true
}

// junkTextKind is the R5.1 junk kind of a whole text, if it is junk.
func junkTextKind(text string, c *ctx) (string, bool) {
	words := splitWords(text)
	if len(words) == 0 {
		return "", false
	}
	return junkKindOf(words, c)
}

// looksLikeYoutube is the R2.1 youtube signals besides platform suffixes.
func looksLikeYoutube(sk skel, c *ctx) bool {
	if len(splitPipes(sk)) > 1 {
		return true
	}
	for _, ig := range groupsOf(sk) {
		if kind, ok := junkTextKind(ig.g.inner, c); ok && kind != "genre" {
			return true
		}
	}
	_, _, ok := trailingJunkPhrase(sk.text, c)
	return ok
}

// prelude is R2 on the normalized string.
func prelude(normalized string, requested requestedMode, c *ctx) preludeResult {
	var junk []positionedJunk
	text := stripPlatformSuffixes([]rune(normalized), c, &junk)
	hadPlatform := len(junk) > 0

	var mode Mode
	switch {
	case requested.kind == requestMode:
		mode = requested.mode // R13.6: tracklist is explicit only; R2.3 and R2.4 do not run.
	case hasExtension(text, c):
		mode = ModeFilename
	default:
		mode = ModeClean
	}

	if mode == ModeFilename {
		// R2.3: strip the extension, trim, then "_" becomes a space when there is no space.
		if hasExtension(text, c) {
			if dot := lastRuneIndex(text, '.'); dot >= 0 {
				text = sub(text, 0, dot)
			}
		}
		text = jsTrimRunes(text)
		if runeIndex(text, ' ') < 0 && runeIndex(text, '_') >= 0 {
			spaced := make([]rune, len(text))
			for i, r := range text {
				if r == '_' {
					r = ' '
				}
				spaced[i] = r
			}
			text = jsTrimRunes(spaced)
		}
	}

	sk, unbalanced := scan(text, 0)
	if unbalanced {
		c.warn(WarningUnbalancedBrackets)
	}
	if requested.kind == requestAuto && mode == ModeClean &&
		(hadPlatform || looksLikeYoutube(sk, c)) {
		mode = ModeYouTube
	}

	if mode != ModeYouTube {
		return preludeResult{mode: mode, working: piece{text: text, base: 0}, junk: junk}
	}
	working, pipeTitle := splitYoutubePipes(sk, c, &junk)
	return preludeResult{mode: mode, working: working, pipeTitle: pipeTitle, junk: junk}
}

// pieceOf is the rendered text of a segment with its absolute offset.
func pieceOf(seg segment) piece {
	return piece{text: []rune(renderSkel(seg.skel)), base: startPos(seg.skel, 0)}
}

// splitYoutubePipes is R2.4.
func splitYoutubePipes(sk skel, c *ctx, junk *[]positionedJunk) (piece, *piece) {
	segments := splitPipes(sk)
	if len(segments) <= 1 {
		return piece{text: []rune(renderSkel(sk)), base: 0}, nil
	}

	var remaining []segment
	for _, seg := range segments {
		text := collapseSpaces(renderSkel(seg.skel))
		// R2.4 step 1: a segment with a spaced dash is an artist/title candidate, never junk.
		kind, isJunk := "", false
		if !hasSpacedDash(seg.skel.text) {
			kind, isJunk = junkTextKind(text, c)
		}
		switch {
		case isJunk && kind != "":
			*junk = append(*junk, positionedJunk{raw: text, kind: kind, pos: startPos(seg.skel, 0)})
		case text != "":
			remaining = append(remaining, seg)
		}
	}
	pushOther := func(keep int) {
		for i, seg := range remaining {
			if i == keep {
				continue
			}
			*junk = append(*junk, positionedJunk{
				raw:  collapseSpaces(renderSkel(seg.skel)),
				kind: "other",
				pos:  startPos(seg.skel, 0),
			})
		}
	}

	for i, seg := range remaining {
		if hasSpacedDash(seg.skel.text) {
			pushOther(i)
			return pieceOf(seg), nil
		}
	}
	if len(remaining) == 2 {
		c.warn(WarningAmbiguousSeparator)
		title := pieceOf(remaining[1])
		return pieceOf(remaining[0]), &title
	}
	if len(remaining) > 0 {
		pushOther(0)
		return pieceOf(remaining[0]), nil
	}
	// Every segment was junk: nothing left to parse.
	return piece{}, nil
}
