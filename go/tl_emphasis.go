package trackparse

// R13.12: Markdown emphasis markers in a tracklist line.

// runLength is the length of the maximal run of '*' or '_' at from (0 when there is none).
func runLength(s []rune, from int) int {
	ch := at(s, from)
	if ch != '*' && ch != '_' {
		return 0
	}
	j := from
	for at(s, j) == ch {
		j++
	}
	return j - from
}

// emphasisRun is R13.2.1 / R13.2.3: the length of an emphasis run at from (a maximal run of 1
// or 2 '*' or '_'), else 0. It is used next to the time, where runs are read by position
// rather than by pairing.
func emphasisRun(s []rune, from int) int {
	if n := runLength(s, from); n <= 2 {
		return n
	}
	return 0
}

// isEmphasisOuter is the R13.12.2 outer side: the line edge, R0.2 whitespace, a bracket, or a
// '*' / '_' of another run.
func isEmphasisOuter(r rune) bool {
	switch r {
	case noRune, '[', ']', '(', ')', '*', '_':
		return true
	}
	return isWhitespace(r)
}

// isEmphasisInner is the R13.12.2 inner side: a character that is not R0.2 whitespace.
func isEmphasisInner(r rune) bool {
	return r != noRune && !isWhitespace(r)
}

// emphasisStack is the stack of a run: one each for '*', '**', '_' and '__'; -1 for longer
// runs and other characters.
func emphasisStack(ch rune, n int) int {
	switch {
	case ch == '*' && n == 1:
		return 0
	case ch == '*' && n == 2:
		return 1
	case ch == '_' && n == 1:
		return 2
	case ch == '_' && n == 2:
		return 3
	}
	return -1
}

// markRange sets marked[from:from+n], clamped.
func markRange(marked []bool, from, n int) {
	for k := from; k < from+n && k < len(marked); k++ {
		if k >= 0 {
			marked[k] = true
		}
	}
}

// withoutMarkers is R13.12: raw[from:] (the line's rest) without its emphasis markers. A run of
// 1 or 2 '*' or '_' closes when the character before it is an inner side and the one after it
// an outer side, and opens the other way round. A closer pairs with the nearest opener of the
// same characters; that opener and every opener after it leave the stack. One stack per run
// ('*', '**', '_', '__') keeps this linear. Runs before from (lead and gap) never pair, except
// that open, a lead run the gap left open (R13.12.4), pairs with a closing run of the same
// character at the end of the line. open is noRune when there is none.
func withoutMarkers(raw []rune, from int, open rune) string {
	marked := make([]bool, len(raw))
	var stacks [4][]int
	if from < 0 {
		from = 0
	}
	for i := from; i < len(raw); {
		n := runLength(raw, i)
		if n < 1 {
			n = 1
		}
		own := emphasisStack(at(raw, i), n)
		prev := before(raw, i)
		next := at(raw, i+n)
		opener := -1
		if own >= 0 && isEmphasisInner(prev) && isEmphasisOuter(next) {
			if st := stacks[own]; len(st) > 0 {
				opener = st[len(st)-1]
				stacks[own] = st[:len(st)-1]
			}
		}
		if opener >= 0 {
			markRange(marked, opener, n)
			markRange(marked, i, n)
			for s := range stacks {
				st := stacks[s]
				for len(st) > 0 && st[len(st)-1] > opener {
					st = st[:len(st)-1]
				}
				stacks[s] = st
			}
		} else if own >= 0 && isEmphasisOuter(prev) && isEmphasisInner(next) {
			stacks[own] = append(stacks[own], i)
		}
		i += n
	}
	// R13.12.4: the last run of the line, after which there is only R0.2 whitespace.
	if open != noRune {
		end := len(raw)
		for end > from && isWhitespace(before(raw, end)) {
			end--
		}
		start := end
		for start > from && before(raw, start) == open {
			start--
		}
		if end-start <= 2 && start > from && isEmphasisInner(before(raw, start)) {
			for k := start; k < end; k++ {
				if marked[k] {
					break
				}
				marked[k] = true
			}
		}
	}
	out := make([]rune, 0, len(raw))
	for k := from; k < len(raw); k++ {
		if !marked[k] {
			out = append(out, raw[k])
		}
	}
	return string(out)
}
