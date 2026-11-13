package trackparse

// ASCII digit scanning shared by R4.1 (prefix.go) and the tracklist time scanner (R13.2).

// digitRun is the length of the run of ASCII digits starting at from.
func digitRun(s []rune, from int) int {
	i := from
	for isDigit(at(s, i)) {
		i++
	}
	return i - from
}

// digitsValue is the value of a run of ASCII digits, saturating at a large bound (callers pass
// at most a few digits). Other code points count as 0.
func digitsValue(s []rune) int {
	const limit = 1 << 40
	n := 0
	for _, r := range s {
		d := 0
		if isDigit(r) {
			d = int(r - '0')
		}
		n = n*10 + d
		if n > limit {
			n = limit
		}
	}
	return n
}

// twoDigitsUpTo59 is the value of exactly two digits at i (not followed by a third) when it is
// at most 59.
func twoDigitsUpTo59(s []rune, i int) (int, bool) {
	if !isDigit(at(s, i)) || !isDigit(at(s, i+1)) || isDigit(at(s, i+2)) {
		return 0, false
	}
	n := digitsValue(sub(s, i, i+2))
	return n, n <= 59
}
