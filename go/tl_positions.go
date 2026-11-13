package trackparse

// R13.5: bare positions.

// bareCandidate is a leading number that may be a position.
type bareCandidate struct {
	raw    string
	number int
	// stripped is the track part with the digits and the following space removed.
	stripped string
}

// findBareCandidate is R13.5: a track part starting with 1 to 3 ASCII digits and a space
// (U+0020 only), where R4.2 took no position (positionTaken), is a bare candidate. The
// remainder must not be empty or only R0.2 whitespace.
func findBareCandidate(rest string, positionTaken bool) *bareCandidate {
	if positionTaken {
		return nil
	}
	t := []rune(rest)
	n := digitRun(t, 0)
	if n < 1 || n > 3 || at(t, n) != ' ' {
		return nil
	}
	stripped := tail(t, n+1)
	blank := true
	for _, r := range stripped {
		if !isWhitespace(r) {
			blank = false
			break
		}
	}
	if blank {
		return nil
	}
	digits := sub(t, 0, n)
	return &bareCandidate{raw: string(digits), number: digitsValue(digits), stripped: string(stripped)}
}

// confirmPositions is R13.5: candidate i is a position when the nearest preceding or the
// nearest following candidate line (R13.4.2, candidateLine) also has a bare candidate equal to
// N - 1 or N + 1. bare holds each line's bare number, or -1 for none. Linear time.
func confirmPositions(bare []int, candidateLine []bool) []bool {
	n := len(bare)
	isCandidate := func(i int) bool { return i >= 0 && i < len(candidateLine) && candidateLine[i] }
	prev := make([]int, n)
	next := make([]int, n)
	last := -1
	for i := 0; i < n; i++ {
		prev[i] = last
		if isCandidate(i) {
			last = i
		}
	}
	last = -1
	for i := n - 1; i >= 0; i-- {
		next[i] = last
		if isCandidate(i) {
			last = i
		}
	}
	adjacent := func(b, j int) bool {
		if j < 0 || j >= n || bare[j] < 0 {
			return false
		}
		o := bare[j]
		return o+1 == b || o == b+1
	}
	out := make([]bool, n)
	for i, b := range bare {
		out[i] = b >= 0 && (adjacent(b, prev[i]) || adjacent(b, next[i]))
	}
	return out
}
