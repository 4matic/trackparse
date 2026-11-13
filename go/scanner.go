package trackparse

// R3: top-level group scanner and skeleton (js/src/scanner.ts, rust/src/scanner.rs).
//
// A skel is a skeleton (code points) plus, for every code point, its absolute offset in the
// normalized input. Offsets are in UTF-16 units, as in the JS reference, so every "order of
// appearance" comparison gives the same answer. Groups are looked up by the offset of their
// placeholder, so any slice of a skeleton still knows which groups it contains.

// ph is the R3.4 placeholder code point. R1.2 removes it from input, so it never occurs there.
const ph rune = 0xe000

// group is one top-level bracket group (R3.1).
type group struct {
	open Delimiter
	// inner is the R3.5 inner text, trimmed and whitespace-collapsed.
	inner string
	// raw is the group exactly as written, brackets included.
	raw string
	// pos is the absolute offset of the opener.
	pos int
}

// groupMap holds the groups of one scan by the offset of their opener. It is only looked up,
// never ranged over, and shared (read-only) by every slice of the skeleton.
type groupMap map[int]*group

// skel is a skeleton: text with each top-level group replaced by ph, and the absolute offset
// of every code point.
type skel struct {
	text   []rune
	pos    []int
	groups groupMap
}

// closerOf is the closing bracket for an opener, or noRune.
func closerOf(r rune) rune {
	switch r {
	case '(':
		return ')'
	case '[':
		return ']'
	case '{':
		return '}'
	}
	return noRune
}

// delimiterOf is the version delimiter of an opener.
func delimiterOf(r rune) Delimiter {
	switch r {
	case '[':
		return DelimiterSquare
	case '{':
		return DelimiterCurly
	}
	return DelimiterParen
}

// scan is R3.1 to R3.5 on s, whose first code point sits at absolute offset base. The second
// result is "unbalanced" (R3.3).
func scan(s []rune, base int) (skel, bool) {
	// The absolute offset of every code point, plus one entry for the end of the string.
	offsets := make([]int, 0, len(s)+1)
	off := base
	for _, r := range s {
		offsets = append(offsets, off)
		if r >= 0x10000 {
			off += 2
		} else {
			off++
		}
	}
	offsets = append(offsets, off)
	offsetAt := func(i int) int {
		if i >= 0 && i < len(offsets) {
			return offsets[i]
		}
		return off
	}

	text := make([]rune, 0, len(s))
	pos := make([]int, 0, len(s))
	groups := groupMap{}
	var stack []rune
	groupStart := 0
	for i, r := range s {
		if len(stack) == 0 {
			if closer := closerOf(r); closer != noRune {
				stack = append(stack, closer)
				groupStart = i
			} else {
				text = append(text, r)
				pos = append(pos, offsetAt(i))
			}
			continue
		}
		if closer := closerOf(r); closer != noRune {
			stack = append(stack, closer)
		} else if stack[len(stack)-1] == r {
			stack = stack[:len(stack)-1]
			if len(stack) == 0 {
				addGroup(s, groupStart, i+1, true, offsetAt, groups)
				text = append(text, ph)
				pos = append(pos, offsetAt(groupStart))
			}
		}
		// R3.2: a non-matching closer is literal text inside the group.
	}
	unbalanced := len(stack) > 0
	if unbalanced {
		// R3.3: the group runs to the end of the string.
		addGroup(s, groupStart, len(s), false, offsetAt, groups)
		text = append(text, ph)
		pos = append(pos, offsetAt(groupStart))
	}
	return skel{text: text, pos: pos, groups: groups}, unbalanced
}

// addGroup records the group s[start:end] (closed: end includes the closer).
func addGroup(s []rune, start, end int, closed bool, offsetAt func(int) int, groups groupMap) {
	innerEnd := end
	if closed {
		innerEnd = end - 1
	}
	p := offsetAt(start)
	groups[p] = &group{
		open:  delimiterOf(at(s, start)),
		inner: collapseRunes(sub(s, start+1, innerEnd)),
		raw:   string(sub(s, start, end)),
		pos:   p,
	}
}

// ---------------------------------------------------------------------------------------------
// Skeleton operations

// clamped is t[a:b] with both ends clamped to the slice (JS slice with non-negative bounds).
func clamped[T any](t []T, a, b int) []T {
	if b > len(t) {
		b = len(t)
	}
	if b < 0 {
		b = 0
	}
	if a > b {
		a = b
	}
	if a < 0 {
		a = 0
	}
	return t[a:b]
}

// len is the number of code points.
func (s skel) len() int { return len(s.text) }

// isEmpty reports whether the skeleton has no code points.
func (s skel) isEmpty() bool { return len(s.text) == 0 }

// sliceSkel is s[start:end], clamped. The text and offsets are copied, the groups shared.
func sliceSkel(s *skel, start, end int) skel {
	return skel{
		text:   append([]rune(nil), clamped(s.text, start, end)...),
		pos:    append([]int(nil), clamped(s.pos, start, end)...),
		groups: s.groups,
	}
}

// sliceFrom is s[start:].
func sliceFrom(s *skel, start int) skel {
	return sliceSkel(s, start, len(s.text))
}

// trimSkel removes R0.2 whitespace at both ends.
func trimSkel(s *skel) skel {
	a, b := 0, len(s.text)
	for a < b && isWhitespace(at(s.text, a)) {
		a++
	}
	for b > a && isWhitespace(before(s.text, b)) {
		b--
	}
	return sliceSkel(s, a, b)
}

// concatSkel is a followed by b (the groups of a).
func concatSkel(a, b *skel) skel {
	text := make([]rune, 0, len(a.text)+len(b.text))
	text = append(append(text, a.text...), b.text...)
	pos := make([]int, 0, len(a.pos)+len(b.pos))
	pos = append(append(pos, a.pos...), b.pos...)
	return skel{text: text, pos: pos, groups: a.groups}
}

// groupAt is the group behind the placeholder at index, or nil.
func groupAt(s *skel, index int) *group {
	if at(s.text, index) != ph || index < 0 || index >= len(s.pos) {
		return nil
	}
	return s.groups[s.pos[index]]
}

// indexedGroup is a group with the index of its placeholder.
type indexedGroup struct {
	index int
	group *group
}

// groupsOf is the groups of the skeleton, left to right.
func groupsOf(s *skel) []indexedGroup {
	var out []indexedGroup
	for i := range s.text {
		if g := groupAt(s, i); g != nil {
			out = append(out, indexedGroup{index: i, group: g})
		}
	}
	return out
}

// hasGroup reports whether the skeleton holds a placeholder.
func hasGroup(s *skel) bool {
	for _, r := range s.text {
		if r == ph {
			return true
		}
	}
	return false
}

// renderSkel substitutes the groups back (R3.4 reassembly). Whitespace is not touched.
func renderSkel(s *skel) string {
	out := make([]rune, 0, len(s.text))
	for i, r := range s.text {
		if g := groupAt(s, i); g != nil {
			out = append(out, []rune(g.raw)...)
		} else {
			out = append(out, r)
		}
	}
	return string(out)
}

// startPos is the absolute offset of the first code point, or fallback for an empty skeleton.
func startPos(s *skel, fallback int) int {
	if len(s.pos) == 0 {
		return fallback
	}
	return s.pos[0]
}

// removeGroups removes the placeholders of the groups at the given offsets.
func removeGroups(s *skel, remove map[int]struct{}) skel {
	if len(remove) == 0 {
		return *s
	}
	text := make([]rune, 0, len(s.text))
	pos := make([]int, 0, len(s.pos))
	for i, r := range s.text {
		if i >= len(s.pos) {
			break
		}
		p := s.pos[i]
		if _, gone := remove[p]; r == ph && gone {
			continue
		}
		text = append(text, r)
		pos = append(pos, p)
	}
	return skel{text: text, pos: pos, groups: s.groups}
}

// isSpacedDashAt is R6.1: "-" with a space immediately on both sides.
func isSpacedDashAt(t []rune, i int) bool {
	return at(t, i) == '-' && before(t, i) == ' ' && at(t, i+1) == ' '
}

// hasSpacedDash reports whether t has a spaced dash.
func hasSpacedDash(t []rune) bool {
	for i := 1; i+1 < len(t); i++ {
		if isSpacedDashAt(t, i) {
			return true
		}
	}
	return false
}

// segment is a trimmed piece of a skeleton and its range inside the parent.
type segment struct {
	skel  skel
	start int
	end   int
}

// trimmedSegment is s[start:end] trimmed of R0.2 whitespace.
func trimmedSegment(s *skel, start, end int) segment {
	a := start
	b := end
	if b < start {
		b = start
	}
	for a < b && isWhitespace(at(s.text, a)) {
		a++
	}
	for b > a && isWhitespace(before(s.text, b)) {
		b--
	}
	return segment{skel: sliceSkel(s, a, b), start: a, end: b}
}

// splitAtSpacedDashes is R6.1 / R8.2: split a skeleton at every spaced dash into trimmed
// segments. With allowLeading, a string starting with "- " yields an empty first segment (R6.3).
func splitAtSpacedDashes(s *skel, allowLeading bool) []segment {
	var segments []segment
	t := s.text
	start := 0
	if allowLeading && at(t, 0) == '-' && at(t, 1) == ' ' {
		segments = append(segments, segment{skel: sliceSkel(s, 0, 0), start: 0, end: 0})
		start = 2
	}
	i := max(start, 1)
	for ; i+1 < len(t); i++ {
		if isSpacedDashAt(t, i) {
			segments = append(segments, trimmedSegment(s, start, i-1))
			start = i + 2
		}
	}
	return append(segments, trimmedSegment(s, start, len(t)))
}
