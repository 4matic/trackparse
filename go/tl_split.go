package trackparse

// R13.1: the line split (LF, CRLF and CR only) with the lossless guarantee.

// tlLine is one line of a tracklist text: the line without its terminator, and the terminator.
type tlLine struct {
	raw string
	eol Eol
}

// splitLines is R13.1.1: split at LF, CRLF and CR. U+2028, U+2029, U+0085 and the other R0.2
// characters are not line breaks. A trailing empty segment after the final terminator is not a
// line, and "" has no lines. R13.1.2: the lines' raw + eol, joined, are the text.
func splitLines(text string) []tlLine {
	var lines []tlLine
	start := 0
	// LF and CR are ASCII and never part of a multi-byte sequence, so every cut is at a
	// character boundary.
	for i := 0; i < len(text); i++ {
		switch text[i] {
		case '\n':
			lines = append(lines, tlLine{raw: text[start:i], eol: EolLF})
			start = i + 1
		case '\r':
			if i+1 < len(text) && text[i+1] == '\n' {
				lines = append(lines, tlLine{raw: text[start:i], eol: EolCRLF})
				i++
			} else {
				lines = append(lines, tlLine{raw: text[start:i], eol: EolCR})
			}
			start = i + 1
		}
	}
	if start < len(text) {
		lines = append(lines, tlLine{raw: text[start:], eol: EolNone})
	}
	return lines
}

// hasLineBreak reports a line break inside a single-line value (R13.9.5, R13.9.6).
func hasLineBreak(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] == '\n' || s[i] == '\r' {
			return true
		}
	}
	return false
}
