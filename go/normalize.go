package trackparse

// R1: normalization (js/src/normalize.ts, rust/src/normalize.rs).

// isRemoved is R1.2: the invisible characters removed outright. ZWNJ and ZWJ (U+200C, U+200D)
// are kept (ZWJ emoji). U+E000 is reserved as the R3.4 group placeholder, so it can never occur
// in input.
func isRemoved(r rune) bool {
	switch r {
	case 0x200b, 0x2060, 0xfeff, 0x00ad, ph:
		return true
	}
	return false
}

// mapRune is R1.3 brackets, R1.4 quotes and R1.5 dashes to ASCII. ok is false for a removed
// character.
func mapRune(r rune) (rune, bool) {
	if isRemoved(r) {
		return 0, false
	}
	if r >= 0x2010 && r <= 0x2015 {
		return '-', true // R1.5
	}
	switch r {
	case 0xff08:
		return '(', true
	case 0xff09:
		return ')', true
	case 0xff3b, 0x3010, 0x3014:
		return '[', true
	case 0xff3d, 0x3011, 0x3015:
		return ']', true
	case 0xff5b:
		return '{', true
	case 0xff5d:
		return '}', true
	case 0x201c, 0x201d, 0x201e, 0x00ab, 0x00bb, 0x300c, 0x300d, 0x300e, 0x300f, 0xff02:
		return '"', true
	case 0x2018, 0x2019, 0x201a, 0x0060, 0x00b4:
		return '\'', true
	case 0x2212, 0xfe58, 0xfe63, 0xff0d:
		return '-', true
	}
	return r, true
}

// collapseSpacedDashRuns is the second half of R1.5: a run of two or more "-" with whitespace
// on both sides becomes one "-".
func collapseSpacedDashRuns(t []rune) []rune {
	out := make([]rune, 0, len(t))
	i := 0
	for i < len(t) {
		r := t[i]
		if r != '-' {
			out = append(out, r)
			i++
			continue
		}
		j := i
		for at(t, j) == '-' {
			j++
		}
		spaced := j-i >= 2 && isWhitespace(before(t, i)) && isWhitespace(at(t, j))
		if spaced {
			out = append(out, '-')
		} else {
			for k := i; k < j; k++ {
				out = append(out, '-')
			}
		}
		i = j
	}
	return out
}

// normalize is R1 on a track string. It is idempotent.
func normalize(input string) string {
	src := nfc(input)
	mapped := make([]rune, 0, len(src))
	for _, r := range src {
		if m, ok := mapRune(r); ok {
			mapped = append(mapped, m)
		}
	}
	// R1.6: every whitespace code point becomes one space, runs collapsed, trimmed. R1.8:
	// removing invisibles can leave a decomposed sequence behind; compose again so that
	// normalize stays idempotent.
	return nfc(collapseRunes(collapseSpacedDashRuns(mapped)))
}
