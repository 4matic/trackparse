package trackparse

// Unicode lookups over the generated tables in unicode_gen.go: general categories (R0.6), JS
// toLowerCase() (R0.4) and NFC/NFD (R1, R0.4). The tables come from Node, the reference engine,
// so the results do not depend on the Go toolchain's Unicode version. Never use
// unicode.IsLetter, unicode.IsUpper, strings.ToLower or golang.org/x/text here.
//
// Every function is total: invalid UTF-8 is read as U+FFFD per bad byte, as `for range` does,
// and nothing panics.

import (
	"slices"
	"unicode/utf8"
)

// uniRange is an inclusive range of code points.
type uniRange struct{ lo, hi rune }

// lowerRun maps first, first+step, ..., up to last, each to itself plus delta.
type lowerRun struct {
	first, last rune
	step        uint8
	delta       int32
}

// lowerMulti is a code point whose lowercase is several code points.
type lowerMulti struct {
	cp rune
	to string
}

// cccRange gives the combining class rank of an inclusive range of code points.
type cccRange struct {
	lo, hi rune
	rank   uint8
}

// decompEntry is the full canonical decomposition of one code point.
type decompEntry struct {
	cp rune
	to string
}

// composePair is a canonical composition: first + second -> composite.
type composePair struct{ first, second, composite rune }

// uniInRanges reports whether r is in one of the sorted, disjoint ranges.
func uniInRanges(table []uniRange, r rune) bool {
	lo, hi := 0, len(table)
	for lo < hi {
		m := int(uint(lo+hi) >> 1)
		switch e := table[m]; {
		case r < e.lo:
			hi = m
		case r > e.hi:
			lo = m + 1
		default:
			return true
		}
	}
	return false
}

// isLetter is the R0.6 letter test: general category L, as JS /\p{L}/u.
func isLetter(r rune) bool {
	if r < 0x80 {
		return r|0x20 >= 'a' && r|0x20 <= 'z'
	}
	return uniInRanges(uniLetter, r)
}

// isUpperLetter is the R6.10 uppercase test: general category Lu, as JS /\p{Lu}/u.
func isUpperLetter(r rune) bool {
	if r < 0x80 {
		return r >= 'A' && r <= 'Z'
	}
	return uniInRanges(uniUpper, r)
}

// ---------------------------------------------------------------------------
// R0.4: JS String.prototype.toLowerCase().

// appendLower appends the lowercase of one code point out of context.
func appendLower(out []byte, r rune) []byte {
	if r < 0x80 {
		if r >= 'A' && r <= 'Z' {
			r += 'a' - 'A'
		}
		return append(out, byte(r))
	}
	// The last run that starts at or before r.
	i, _ := slices.BinarySearchFunc(uniLower, r, func(e lowerRun, t rune) int {
		if e.first <= t {
			return -1
		}
		return 1
	})
	if i > 0 {
		e := uniLower[i-1]
		if r <= e.last && (r-e.first)%rune(e.step) == 0 {
			return utf8.AppendRune(out, r+e.delta)
		}
	}
	if j, ok := slices.BinarySearchFunc(uniLowerMulti, r, func(e lowerMulti, t rune) int {
		return int(e.cp - t)
	}); ok {
		return append(out, uniLowerMulti[j].to...)
	}
	return utf8.AppendRune(out, r)
}

// casedBefore is the final-sigma rule's "a cased letter precedes", looking back through
// case-ignorable code points. A code point that is both cased and case-ignorable (U+0345,
// modifier letters) is skipped, as in ICU, which is what JS engines run.
func casedBefore(s string) bool {
	for len(s) > 0 {
		r, n := utf8.DecodeLastRuneInString(s)
		if !uniInRanges(uniCaseIgnorable, r) {
			return uniInRanges(uniCased, r)
		}
		s = s[:len(s)-n]
	}
	return false
}

// casedAfter is casedBefore looking forward.
func casedAfter(s string) bool {
	for _, r := range s {
		if !uniInRanges(uniCaseIgnorable, r) {
			return uniInRanges(uniCased, r)
		}
	}
	return false
}

// jsToLower is JS toLowerCase() (R0.4), from the generated tables. The one context rule:
// U+03A3 is U+03C2 after a cased letter when no cased letter follows (Final_Sigma), else U+03C3.
func jsToLower(s string) string {
	ascii, upper := true, false
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c >= 0x80 {
			ascii = false
			break
		}
		if c >= 'A' && c <= 'Z' {
			upper = true
		}
	}
	if ascii && !upper {
		return s
	}
	out := make([]byte, 0, len(s))
	if ascii {
		for i := 0; i < len(s); i++ {
			c := s[i]
			if c >= 'A' && c <= 'Z' {
				c += 'a' - 'A'
			}
			out = append(out, c)
		}
		return string(out)
	}
	for i, r := range s {
		if r != 0x3a3 {
			out = appendLower(out, r)
			continue
		}
		if casedBefore(s[:i]) && !casedAfter(s[i+2:]) {
			out = utf8.AppendRune(out, 0x3c2)
		} else {
			out = utf8.AppendRune(out, 0x3c3)
		}
	}
	return string(out)
}

// ---------------------------------------------------------------------------
// R1, R0.4: canonical normalization (Unicode Standard Annex #15), identical to JS
// String.prototype.normalize("NFC") and normalize("NFD").

// Hangul syllables, decomposed and composed algorithmically (Unicode 3.12).
const (
	hangulSBase  = 0xac00
	hangulLBase  = 0x1100
	hangulVBase  = 0x1161
	hangulTBase  = 0x11a7
	hangulLCount = 19
	hangulVCount = 21
	hangulTCount = 28
	hangulNCount = hangulVCount * hangulTCount
	hangulSCount = hangulLCount * hangulNCount
)

// combiningClass is the canonical combining class rank of r (0 for starters). Only meaningful for code
// points that are their own NFD, and for the NFC quick check (see uniCCC).
func combiningClass(r rune) uint8 {
	if r < uniQuickMin {
		return 0
	}
	lo, hi := 0, len(uniCCC)
	for lo < hi {
		m := int(uint(lo+hi) >> 1)
		switch e := uniCCC[m]; {
		case r < e.lo:
			hi = m
		case r > e.hi:
			lo = m + 1
		default:
			return e.rank
		}
	}
	return 0
}

// uniDecomposition returns the full canonical decomposition of r from the table, or "" when r has
// none (Hangul syllables are handled by the caller).
func uniDecomposition(r rune) string {
	if r < uniQuickMin {
		return ""
	}
	if i, ok := slices.BinarySearchFunc(uniDecomp, r, func(e decompEntry, t rune) int {
		return int(e.cp - t)
	}); ok {
		return uniDecomp[i].to
	}
	return ""
}

func isHangulSyllable(r rune) bool { return r >= hangulSBase && r < hangulSBase+hangulSCount }

// composeTwo returns the primary composite of a + b, if there is one.
func composeTwo(a, b rune) (rune, bool) {
	// Hangul: L + V -> LV, LV + T -> LVT.
	if a >= hangulLBase && a < hangulLBase+hangulLCount && b >= hangulVBase && b < hangulVBase+hangulVCount {
		return hangulSBase + ((a-hangulLBase)*hangulVCount+(b-hangulVBase))*hangulTCount, true
	}
	if isHangulSyllable(a) && (a-hangulSBase)%hangulTCount == 0 && b > hangulTBase && b < hangulTBase+hangulTCount {
		return a + (b - hangulTBase), true
	}
	i, ok := slices.BinarySearchFunc(uniCompose, [2]rune{a, b}, func(e composePair, t [2]rune) int {
		if e.first != t[0] {
			return int(e.first - t[0])
		}
		return int(e.second - t[1])
	})
	if !ok {
		return 0, false
	}
	return uniCompose[i].composite, true
}

// uniIsASCII reports whether s is all ASCII bytes.
func uniIsASCII(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] >= 0x80 {
			return false
		}
	}
	return true
}

// nfdQuick reports whether s is already NFD: valid UTF-8, nothing that decomposes and the
// non-starters in canonical order.
func nfdQuick(s string) bool {
	var last uint8
	for i := 0; i < len(s); {
		r, n := rune(s[i]), 1
		if r >= utf8.RuneSelf {
			r, n = utf8.DecodeRuneInString(s[i:])
			if r == utf8.RuneError && n == 1 {
				return false
			}
		}
		i += n
		if r < uniQuickMin {
			last = 0
			continue
		}
		if isHangulSyllable(r) || uniDecomposition(r) != "" {
			return false
		}
		cc := combiningClass(r)
		if cc != 0 && last > cc {
			return false
		}
		last = cc
	}
	return true
}

// nfcQuick is the NFC quick check of UAX #15 section 9, with Maybe treated as No: true only when
// s is certainly NFC.
func nfcQuick(s string) bool {
	var last uint8
	for i := 0; i < len(s); {
		r, n := rune(s[i]), 1
		if r >= utf8.RuneSelf {
			r, n = utf8.DecodeRuneInString(s[i:])
			if r == utf8.RuneError && n == 1 {
				return false
			}
		}
		i += n
		if r < uniQuickMin {
			last = 0
			continue
		}
		if uniInRanges(uniNFCNo, r) || uniInRanges(uniComposeSecond, r) {
			return false
		}
		cc := combiningClass(r)
		if cc != 0 && last > cc {
			return false
		}
		last = cc
	}
	return true
}

// nfdRunes returns the canonical decomposition of s in canonical order (NFD as runes).
func nfdRunes(s string) []rune {
	buf := make([]rune, 0, len(s)+len(s)/2)
	for _, r := range s {
		switch {
		case r < uniQuickMin:
			buf = append(buf, r)
		case isHangulSyllable(r):
			si := r - hangulSBase
			buf = append(buf, hangulLBase+si/hangulNCount, hangulVBase+(si%hangulNCount)/hangulTCount)
			if t := si % hangulTCount; t != 0 {
				buf = append(buf, hangulTBase+t)
			}
		default:
			if d := uniDecomposition(r); d != "" {
				for _, x := range d {
					buf = append(buf, x)
				}
			} else {
				buf = append(buf, r)
			}
		}
	}
	canonicalOrder(buf)
	return buf
}

// canonicalOrder applies the canonical ordering algorithm: each run of non-starters is sorted by
// combining class, stably.
func canonicalOrder(buf []rune) {
	for i := 0; i < len(buf); {
		if combiningClass(buf[i]) == 0 {
			i++
			continue
		}
		j := i + 1
		for j < len(buf) && combiningClass(buf[j]) != 0 {
			j++
		}
		run := buf[i:j]
		if len(run) <= 8 {
			// Insertion sort: stable, and runs are almost always short.
			for k := 1; k < len(run); k++ {
				x := run[k]
				cx := combiningClass(x)
				m := k
				for m > 0 && combiningClass(run[m-1]) > cx {
					run[m] = run[m-1]
					m--
				}
				run[m] = x
			}
		} else {
			slices.SortStableFunc(run, func(a, b rune) int { return int(combiningClass(a)) - int(combiningClass(b)) })
		}
		i = j
	}
}

// canonicalCompose applies the canonical composition algorithm to decomposed, canonically ordered
// runes, in place, and returns the composed prefix.
func canonicalCompose(buf []rune) []rune {
	starter := -1 // index in the output of the last starter, -1 before the first
	var last uint8
	w := 0
	for _, c := range buf {
		if c < uniQuickMin {
			// A starter that composes with nothing before it.
			starter, last = w, 0
			buf[w] = c
			w++
			continue
		}
		cc := combiningClass(c)
		if starter >= 0 {
			// c is blocked from the starter when something sits between them whose class is 0
			// or not lower than c's.
			if w-1 == starter || (last != 0 && last < cc) {
				if p, ok := composeTwo(buf[starter], c); ok {
					buf[starter] = p
					continue
				}
			}
		}
		if cc == 0 {
			starter = w
		}
		last = cc
		buf[w] = c
		w++
	}
	return buf[:w]
}

// nfd is JS s.normalize("NFD"). It returns s itself, without allocating, when s is already NFD.
func nfd(s string) string {
	if uniIsASCII(s) || nfdQuick(s) {
		return s
	}
	return string(nfdRunes(s))
}

// nfc is JS s.normalize("NFC"). It returns s itself, without allocating, when s is already NFC.
func nfc(s string) string {
	if uniIsASCII(s) || nfcQuick(s) {
		return s
	}
	return string(canonicalCompose(nfdRunes(s)))
}
