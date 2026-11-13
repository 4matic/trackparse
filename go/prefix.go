package trackparse

// R4: leading timestamp and track-position prefixes (js/src/prefix.ts, rust/src/prefix.rs).

// prefixResult is the input after R4.
type prefixResult struct {
	rest []rune
	// base is the absolute offset of rest.
	base      int
	timestamp *Timestamp
	position  *Position
}

// timeMatch is a time at the start of a prefix.
type timeMatch struct {
	raw     string
	seconds int
	end     int
}

// matchTime is H:MM:SS, HH:MM:SS, M:SS or MM:SS starting at from (R4.1).
func matchTime(s []rune, from int) (timeMatch, bool) {
	lead := digitRun(s, from)
	if lead < 1 || lead > 2 || at(s, from+lead) != ':' {
		return timeMatch{}, false
	}
	first := digitsValue(sub(s, from, from+lead))
	mid, ok := twoDigitsUpTo59(s, from+lead+1)
	if !ok {
		return timeMatch{}, false
	}
	afterMid := from + lead + 3
	if at(s, afterMid) == ':' {
		if sec, ok := twoDigitsUpTo59(s, afterMid+1); ok {
			end := afterMid + 3
			return timeMatch{
				raw:     string(sub(s, from, end)),
				seconds: first*3600 + mid*60 + sec,
				end:     end,
			}, true
		}
	}
	if lead == 2 && first > 59 {
		return timeMatch{}, false // MM is 00-59
	}
	return timeMatch{raw: string(sub(s, from, afterMid)), seconds: first*60 + mid, end: afterMid}, true
}

// skipSpaceAndDash skips, after a prefix, one space and then an optional "- " (spaced dash).
func skipSpaceAndDash(s []rune, from int) int {
	i := from
	if at(s, i) == ' ' {
		i++
	}
	if at(s, i) == '-' && at(s, i+1) == ' ' {
		i += 2
	}
	return i
}

// matchTimestamp is R4.1: the timestamp and the index where the rest starts.
func matchTimestamp(s []rune) (Timestamp, int, bool) {
	closer := noRune
	switch at(s, 0) {
	case '[':
		closer = ']'
	case '(':
		closer = ')'
	}
	from := 0
	if closer != noRune {
		from = 1
	}
	t, ok := matchTime(s, from)
	if !ok {
		return Timestamp{}, 0, false
	}
	end := t.end
	if closer != noRune {
		if at(s, end) != closer {
			return Timestamp{}, 0, false
		}
		end++
	}
	if end < len(s) && at(s, end) != ' ' {
		return Timestamp{}, 0, false
	}
	return Timestamp{Raw: t.raw, Seconds: t.seconds}, skipSpaceAndDash(s, end), true
}

// containsSpacedDash reports whether the skeleton of s has a spaced dash.
func containsSpacedDash(s []rune) bool {
	sk, _ := scan(s, 0)
	return hasSpacedDash(sk.text)
}

// matchPosition is R4.2: the end index of the position prefix. Forms are tried (a), (b), (c),
// then (d) and (e).
func matchPosition(s []rune, mode Mode) (int, bool) {
	n := digitRun(s, 0)
	if n < 1 || n > 3 {
		return 0, false
	}
	c := at(s, n)
	zeroPadded := at(s, 0) == '0' && n >= 2
	// (a) "01. ", "1) "
	if (c == '.' || c == ')') && (at(s, n+1) == ' ' || n+1 == len(s)) {
		return skipSpaceAndDash(s, n+1), true
	}
	// (b) "01 ": a position in every mode.
	if zeroPadded && c == ' ' {
		return skipSpaceAndDash(s, n), true
	}
	// (c) "3 - Artist - Title"; in filename mode "N - " is always a position ("311 - Amber.mp3").
	if c == ' ' && at(s, n+1) == '-' && at(s, n+2) == ' ' {
		if mode == ModeFilename || containsSpacedDash(tail(s, n+3)) {
			return n + 3, true
		}
		return 0, false
	}
	if mode == ModeFilename {
		// (e) "01-Track", "03_name", "01.Track"
		if (c == '-' || c == '_' || c == '.') && at(s, n+1) != noRune && at(s, n+1) != ' ' {
			return n + 1, true
		}
		// (e) "3 My Song.mp3": only when the remainder has no spaced dash.
		if c == ' ' && !containsSpacedDash(tail(s, n+1)) {
			return skipSpaceAndDash(s, n), true
		}
	}
	// (d) "2 Unlimited", "50 Cent": never a position outside filename mode.
	return 0, false
}

// stripPrefixes is R4 on text, whose first code point sits at absolute offset base.
func stripPrefixes(text []rune, base int, mode Mode, c *ctx) prefixResult {
	rest := text
	offset := base
	var timestamp *Timestamp
	var position *Position

	if ts, end, ok := matchTimestamp(rest); ok {
		timestamp = &ts
		offset += utf16Len(sub(rest, 0, end))
		rest = tail(rest, end)
	}
	// R7.3a: a known artist that starts with digits is not a position.
	if matchKnownAt(rest, 0, c, '-') == 0 {
		if end, ok := matchPosition(rest, mode); ok && end < len(rest) {
			digits := sub(rest, 0, digitRun(rest, 0))
			position = &Position{Raw: string(digits), Number: digitsValue(digits)}
			offset += utf16Len(sub(rest, 0, end))
			rest = tail(rest, end)
		}
	}
	return prefixResult{rest: rest, base: offset, timestamp: timestamp, position: position}
}
