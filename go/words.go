package trackparse

import "unicode/utf8"

// Low-level text helpers shared by every rule: the R0.2 whitespace set, R0.3 word keys, R0.4
// dedup keys, R0.6 letters and digits, token-sequence (phrase) matching, and the input
// sanitizer for invalid UTF-8.
//
// Text that is indexed is held as []rune, so indices are code-point indices (R0.5). Where the
// JS reference observes UTF-16 lengths (utf16Len), that is made explicit. Never index a string
// by byte inside a rule. The letter tests and the JS lowercase/NFC/NFD functions (isLetter,
// isUpperLetter, jsToLower, nfc, nfd) live in unicode.go, generated from the reference engine.

// noRune is what at and before give outside the slice (JS t[i] is undefined there). It is not a
// valid code point, so it never equals a character and fails every class test.
const noRune rune = -1

// at is t[i], or noRune out of range.
func at(t []rune, i int) rune {
	if i < 0 || i >= len(t) {
		return noRune
	}
	return t[i]
}

// before is t[i-1], or noRune when i is 0 or out of range.
func before(t []rune, i int) rune {
	return at(t, i-1)
}

// sub is t[a:b] with both ends clamped (JS slice with non-negative arguments).
func sub(t []rune, a, b int) []rune {
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

// tail is t[a:], clamped.
func tail(t []rune, a int) []rune {
	return sub(t, a, len(t))
}

// isWhitespace is the R0.2 test: the exact whitespace code-point set. Never use
// unicode.IsSpace or strings.TrimSpace, whose sets differ.
func isWhitespace(r rune) bool {
	return (r >= 0x09 && r <= 0x0d) ||
		r == 0x20 ||
		r == 0x85 ||
		r == 0xa0 ||
		r == 0x1680 ||
		(r >= 0x2000 && r <= 0x200a) ||
		r == 0x2028 ||
		r == 0x2029 ||
		r == 0x202f ||
		r == 0x205f ||
		r == 0x3000
}

// isJSSpace is the set of JS String.prototype.trim(): WhiteSpace (with U+FEFF and Zs) plus
// LineTerminator. Not the R0.2 set: U+FEFF is in, U+0085 is out.
func isJSSpace(r rune) bool {
	return (r >= 0x09 && r <= 0x0d) ||
		r == 0x20 ||
		r == 0xa0 ||
		r == 0x1680 ||
		(r >= 0x2000 && r <= 0x200a) ||
		r == 0x2028 ||
		r == 0x2029 ||
		r == 0x202f ||
		r == 0x205f ||
		r == 0x3000 ||
		r == 0xfeff
}

// jsTrim is JS trim().
func jsTrim(s string) string {
	return jsTrimEnd(jsTrimStart(s))
}

// jsTrimStart is JS trimStart().
func jsTrimStart(s string) string {
	for len(s) > 0 {
		r, size := utf8.DecodeRuneInString(s)
		if !isJSSpace(r) {
			break
		}
		s = s[size:]
	}
	return s
}

// jsTrimEnd is JS trimEnd().
func jsTrimEnd(s string) string {
	for len(s) > 0 {
		r, size := utf8.DecodeLastRuneInString(s)
		if !isJSSpace(r) {
			break
		}
		s = s[:len(s)-size]
	}
	return s
}

// jsTrimRunes is JS trim() on a code-point slice.
func jsTrimRunes(t []rune) []rune {
	a, b := 0, len(t)
	for a < b && isJSSpace(t[a]) {
		a++
	}
	for b > a && isJSSpace(t[b-1]) {
		b--
	}
	return t[a:b]
}

// isDigit is the R0.6 digit test: ASCII 0-9 only.
func isDigit(r rune) bool {
	return r >= '0' && r <= '9'
}

// isAllDigits reports whether s is a non-empty run of ASCII digits.
func isAllDigits(s string) bool {
	if s == "" {
		return false
	}
	for i := 0; i < len(s); i++ {
		if s[i] < '0' || s[i] > '9' {
			return false
		}
	}
	return true
}

// isLetterAt is the R0.6 letter test (general category L) on the code point at i.
func isLetterAt(t []rune, i int) bool {
	r := at(t, i)
	return r >= 0 && isLetter(r)
}

// isLetterBefore is the R0.6 letter test on the code point that ends just before i.
func isLetterBefore(t []rune, i int) bool {
	return isLetterAt(t, i-1)
}

// isUpperAt is the R6.10 uppercase test (general category Lu) on the code point at i.
func isUpperAt(t []rune, i int) bool {
	r := at(t, i)
	return r >= 0 && isUpperLetter(r)
}

// asciiLower lowercases A-Z only.
func asciiLower(s string) string {
	for i := 0; i < len(s); i++ {
		if c := s[i]; c >= 'A' && c <= 'Z' {
			b := []byte(s)
			for j := i; j < len(b); j++ {
				if b[j] >= 'A' && b[j] <= 'Z' {
					b[j] += 'a' - 'A'
				}
			}
			return string(b)
		}
	}
	return s
}

// stripKeyPunct is R0.3: the lowercased word with one trailing . , ! ? : ; removed.
func stripKeyPunct(lower string) string {
	if n := len(lower); n > 0 {
		switch lower[n-1] {
		case '.', ',', '!', '?', ':', ';':
			return lower[:n-1]
		}
	}
	return lower
}

// wordKey is the R0.3 word key (stripped form).
func wordKey(word string) string {
	return stripKeyPunct(asciiLower(word))
}

// wordMatches is R0.3: does word match the vocabulary token? The unstripped lowercase form is
// compared first, so entries ending in punctuation (feat.) match as written.
func wordMatches(word, token string) bool {
	lower := asciiLower(word)
	return lower == token || stripKeyPunct(lower) == token
}

// wordSet is a set of vocabulary words.
type wordSet map[string]struct{}

// newWordSet is the set of the given words.
func newWordSet(words ...[]string) wordSet {
	set := wordSet{}
	for _, list := range words {
		for _, w := range list {
			set[w] = struct{}{}
		}
	}
	return set
}

// has reports whether key is in the set.
func (s wordSet) has(key string) bool {
	_, ok := s[key]
	return ok
}

// inVocab is R0.3 lookup of a word in a vocabulary set: its lowercase form, then its stripped
// form.
func inVocab(word string, vocab wordSet) bool {
	lower := asciiLower(word)
	return vocab.has(lower) || vocab.has(stripKeyPunct(lower))
}

// collapseSpaces collapses runs of R0.2 whitespace to one U+0020 and trims.
func collapseSpaces(s string) string {
	out := make([]rune, 0, len(s))
	pending := false
	for _, r := range s {
		if isWhitespace(r) {
			pending = len(out) > 0
			continue
		}
		if pending {
			out = append(out, ' ')
		}
		pending = false
		out = append(out, r)
	}
	return string(out)
}

// collapseRunes is collapseSpaces on a code-point slice.
func collapseRunes(t []rune) string {
	return collapseSpaces(string(t))
}

// splitWords is R0.7: the maximal runs of non-whitespace code points.
func splitWords(s string) []string {
	return splitWordsRunes([]rune(s))
}

// splitWordsRunes is splitWords on a code-point slice.
func splitWordsRunes(t []rune) []string {
	spans := wordSpans(t)
	out := make([]string, len(spans))
	for i, w := range spans {
		out[i] = w.text
	}
	return out
}

// wordSpan is a word with its code-point span inside the slice it was taken from.
type wordSpan struct {
	text  string
	start int
	end   int
}

// wordSpans is the R0.7 words of t with their spans.
func wordSpans(t []rune) []wordSpan {
	var spans []wordSpan
	start := -1
	for i := 0; i <= len(t); i++ {
		if i == len(t) || isWhitespace(t[i]) {
			if start >= 0 {
				spans = append(spans, wordSpan{text: string(t[start:i]), start: start, end: i})
			}
			start = -1
		} else if start < 0 {
			start = i
		}
	}
	return spans
}

// spanTexts is the text of each span.
func spanTexts(spans []wordSpan) []string {
	out := make([]string, len(spans))
	for i, s := range spans {
		out[i] = s.text
	}
	return out
}

// dedupKey is the R0.4 dedup key: JS toLowerCase(), NFD, U+0300-U+036F removed, whitespace
// collapsed. The only place with full Unicode lowercasing.
func dedupKey(s string) string {
	decomposed := nfd(jsToLower(s))
	kept := make([]rune, 0, len(decomposed))
	for _, r := range decomposed {
		if r >= 0x300 && r <= 0x36f {
			continue
		}
		kept = append(kept, r)
	}
	return collapseRunes(kept)
}

// yearOf is a year word: 4 ASCII digits in 1900-2099 (word key, so "2015," counts).
func yearOf(word string) (int, bool) {
	key := wordKey(word)
	if len(key) != 4 || !isAllDigits(key) {
		return 0, false
	}
	n := int(key[0]-'0')*1000 + int(key[1]-'0')*100 + int(key[2]-'0')*10 + int(key[3]-'0')
	return n, n >= 1900 && n <= 2099
}

// utf16Len is the length in UTF-16 code units (JS string.length). Offsets that order
// fragments (R9) are kept in these units so ties break exactly as in the JS reference.
func utf16Len(t []rune) int {
	n := 0
	for _, r := range t {
		if r >= 0x10000 {
			n += 2
		} else {
			n++
		}
	}
	return n
}

// utf16LenString is utf16Len of a string.
func utf16LenString(s string) int {
	n := 0
	for _, r := range s {
		if r >= 0x10000 {
			n += 2
		} else {
			n++
		}
	}
	return n
}

// sanitize replaces every maximal subpart of an ill-formed UTF-8 sequence with one U+FFFD
// (the WHATWG / Unicode "maximal subpart" practice of TextDecoder and of Rust's
// String::from_utf8_lossy), so the Go and Rust ports read the same bytes as the same text.
// Go's own decoding gives one U+FFFD per bad byte instead. Valid input is returned unchanged.
func sanitize(s string) string {
	if utf8.ValidString(s) {
		return s
	}
	out := make([]byte, 0, len(s)+8)
	i := 0
	for i < len(s) {
		c := s[i]
		if c < 0x80 {
			out = append(out, c)
			i++
			continue
		}
		// The bounds of the second byte, and how many continuation bytes follow the lead.
		lo, hi, need := byte(0x80), byte(0xbf), 0
		switch {
		case c >= 0xc2 && c <= 0xdf:
			need = 1
		case c == 0xe0:
			lo, need = 0xa0, 2
		case c >= 0xe1 && c <= 0xec, c == 0xee, c == 0xef:
			need = 2
		case c == 0xed:
			hi, need = 0x9f, 2
		case c == 0xf0:
			lo, need = 0x90, 3
		case c >= 0xf1 && c <= 0xf3:
			need = 3
		case c == 0xf4:
			hi, need = 0x8f, 3
		}
		if need == 0 {
			out = utf8.AppendRune(out, utf8.RuneError)
			i++
			continue
		}
		j := i + 1
		ok := true
		for k := 0; k < need; k++ {
			if j >= len(s) {
				ok = false
				break
			}
			b := s[j]
			if k > 0 {
				lo, hi = 0x80, 0xbf
			}
			if b < lo || b > hi {
				ok = false
				break
			}
			j++
		}
		if ok {
			out = append(out, s[i:j]...)
		} else {
			// The lead and the valid continuation bytes so far are one maximal subpart.
			out = utf8.AppendRune(out, utf8.RuneError)
		}
		i = j
	}
	return string(out)
}

// ---------------------------------------------------------------------------------------------
// Phrase tables: multi-word vocabulary matched as token sequences, longest first.

type phraseEntry[V any] struct {
	tokens []string
	value  V
}

// phraseMatch is a match of a phrase table: the entry's value and the number of words used.
type phraseMatch[V any] struct {
	value  V
	length int
}

// phraseTable is multi-word vocabulary. The maps are only looked up, never ranged over.
type phraseTable[V any] struct {
	entries []phraseEntry[V]
	byFirst map[string][]int
	byLast  map[string][]int
	keys    map[string]struct{}
}

// newPhraseTable is an empty table.
func newPhraseTable[V any]() *phraseTable[V] {
	return &phraseTable[V]{
		byFirst: map[string][]int{},
		byLast:  map[string][]int{},
		keys:    map[string]struct{}{},
	}
}

// add adds a phrase. The first entry for a key wins. Keys are ASCII-lowercased and trimmed like
// JS trim() (not collapsed).
func (t *phraseTable[V]) add(rawKey string, value V) {
	key := jsTrim(asciiLower(rawKey))
	tokens := splitWords(key)
	if len(tokens) == 0 {
		return
	}
	if _, dup := t.keys[key]; dup {
		return
	}
	id := len(t.entries)
	t.keys[key] = struct{}{}
	t.entries = append(t.entries, phraseEntry[V]{tokens: tokens, value: value})
	t.insertSorted(t.byFirst, tokens[0], id)
	t.insertSorted(t.byLast, tokens[len(tokens)-1], id)
}

// insertSorted keeps each list longest first, insertion order among equals (a stable insert,
// like the JS sort).
func (t *phraseTable[V]) insertSorted(m map[string][]int, k string, id int) {
	list := append(m[k], id)
	n := len(t.entries[id].tokens)
	for i := len(list) - 1; i > 0 && len(t.entries[list[i-1]].tokens) < n; i-- {
		list[i], list[i-1] = list[i-1], list[i]
	}
	m[k] = list
}

// candidates are the entries indexed under the word's lowercase form, then under its stripped
// form.
func (t *phraseTable[V]) candidates(m map[string][]int, word string) []int {
	lower := asciiLower(word)
	stripped := stripKeyPunct(lower)
	a := m[lower]
	if stripped == lower {
		return a
	}
	b := m[stripped]
	if len(b) == 0 {
		return a
	}
	out := make([]int, 0, len(a)+len(b))
	return append(append(out, a...), b...)
}

// matchAt is the longest entry matching words[start:].
func (t *phraseTable[V]) matchAt(words []string, start int) (phraseMatch[V], bool) {
	var best phraseMatch[V]
	found := false
	if start < 0 || start >= len(words) {
		return best, false
	}
	for _, id := range t.candidates(t.byFirst, words[start]) {
		e := &t.entries[id]
		n := len(e.tokens)
		if found && n <= best.length {
			continue
		}
		if start+n > len(words) {
			continue
		}
		if tokensMatch(words, start, e.tokens) {
			best, found = phraseMatch[V]{value: e.value, length: n}, true
		}
	}
	return best, found
}

// matchEnding is the longest entry matching words[:end] at its end (end exclusive).
func (t *phraseTable[V]) matchEnding(words []string, end int) (phraseMatch[V], bool) {
	var best phraseMatch[V]
	found := false
	if end < 1 || end > len(words) {
		return best, false
	}
	for _, id := range t.candidates(t.byLast, words[end-1]) {
		e := &t.entries[id]
		n := len(e.tokens)
		if found && n <= best.length {
			continue
		}
		from := end - n
		if from < 0 {
			continue
		}
		if tokensMatch(words, from, e.tokens) {
			best, found = phraseMatch[V]{value: e.value, length: n}, true
		}
	}
	return best, found
}

func tokensMatch(words []string, start int, tokens []string) bool {
	for k, tok := range tokens {
		i := start + k
		if i < 0 || i >= len(words) || !wordMatches(words[i], tok) {
			return false
		}
	}
	return true
}
