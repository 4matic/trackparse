package trackparse

import (
	"strings"
	"sync"
)

// R13.3 line kinds, R13.4.2 candidate lines and the R13.13 layered prefix.

var (
	tlHeaderWords  = sync.OnceValue(func() wordSet { return newWordSet(dataHeaderWordsWords) })
	tlLayeredWords = sync.OnceValue(func() wordSet { return newWordSet(dataLayeredWordsWords) })
	tlMarkerTable  = sync.OnceValue(func() *phraseTable[struct{}] { return buildMarkerTable(nil) })
)

// buildMarkerTable is data/marker-words.json plus extra. Entries are split into words the same
// way as the track part (R13.3), so shout-out matches the words shout, out.
func buildMarkerTable(extra []string) *phraseTable[struct{}] {
	t := newPhraseTable[struct{}]()
	for _, list := range [][]string{dataMarkerWordsWords, extra} {
		for _, w := range list {
			t.add(strings.Join(markerWords(w), " "), struct{}{})
		}
	}
	return t
}

// markerTableFor is the marker table with options.markerWords (R13.10); the default table is
// built once.
func markerTableFor(extra []string) *phraseTable[struct{}] {
	if len(extra) == 0 {
		return tlMarkerTable()
	}
	return buildMarkerTable(extra)
}

// isMarkerBreak splits marker words: '/', '-' and R0.2 whitespace.
func isMarkerBreak(r rune) bool {
	return r == '/' || r == '-' || isWhitespace(r)
}

// isMarkerBracket is one of ()[]{}.
func isMarkerBracket(r rune) bool {
	switch r {
	case '(', ')', '[', ']', '{', '}':
		return true
	}
	return false
}

// markerWords is R13.3: split on R0.2 whitespace, '/' and '-', then strip leading and trailing
// ()[]{} from each piece; empty pieces are dropped. Word keys (R0.3) apply on matching.
func markerWords(text string) []string {
	var out []string
	var current []rune
	flush := func() {
		a, b := 0, len(current)
		for a < b && isMarkerBracket(current[a]) {
			a++
		}
		for b > a && isMarkerBracket(current[b-1]) {
			b--
		}
		if b > a {
			out = append(out, string(current[a:b]))
		}
		current = current[:0]
	}
	for _, r := range text {
		if isMarkerBreak(r) {
			flush()
		} else {
			current = append(current, r)
		}
	}
	flush()
	return out
}

// layeredPrefix is R13.13: the length (in code points) of a layered prefix at the start of the
// line text: R0.2 whitespace, a word of data/layered-words.json (word key, R0.3), then R0.2
// whitespace, with more text after it. 0 when there is none.
func layeredPrefix(text []rune) int {
	i := skipWhitespace(text, 0)
	j := i
	for j < len(text) && !isWhitespace(text[j]) {
		j++
	}
	if j == i || j == len(text) || !inVocab(string(text[i:j]), tlLayeredWords()) {
		return 0
	}
	j = skipWhitespace(text, j)
	if j < len(text) {
		return j
	}
	return 0
}

// isBlank is R13.3.1: the line is only R0.2 whitespace.
func isBlank(raw string) bool {
	for _, r := range raw {
		if !isWhitespace(r) {
			return false
		}
	}
	return true
}

// trimWS trims R0.2 whitespace from both ends.
func trimWS(s string) []rune {
	t := []rune(s)
	a, b := 0, len(t)
	for a < b && isWhitespace(t[a]) {
		a++
	}
	for b > a && isWhitespace(t[b-1]) {
		b--
	}
	return t[a:b]
}

// isLink is R13.3.3: the text starts with http://, https:// or www. (ASCII case-insensitive).
func isLink(rest string) bool {
	t := asciiLower(string(sub(trimWS(rest), 0, 8)))
	return strings.HasPrefix(t, "http://") || strings.HasPrefix(t, "https://") ||
		strings.HasPrefix(t, "www.")
}

// isHeader is R13.3.3: the text ends with ':', or is a header word followed by at most one more
// word.
func isHeader(rest string) bool {
	t := trimWS(rest)
	if len(t) > 0 && t[len(t)-1] == ':' {
		return true
	}
	words := splitWordsRunes(t)
	return len(words) > 0 && len(words) <= 2 && inVocab(words[0], tlHeaderWords())
}

// hasMarkerWord is R13.3: any marker word (or multi-word entry) among the track part's marker
// words.
func hasMarkerWord(trackPart string, markers *phraseTable[struct{}]) bool {
	words := markerWords(trackPart)
	for i := range words {
		if _, ok := markers.matchAt(words, i); ok {
			return true
		}
	}
	return false
}

// hasSeparator is R13.3: the track part has a separator. It is not empty after R1 (empty), its
// parse did not warn noSeparator, and the artist side is not empty (at least one primary
// artist, or flags.unknownArtist).
func hasSeparator(t *Track, empty bool) bool {
	if empty {
		return false
	}
	for _, w := range t.Warnings {
		if w == WarningNoSeparator {
			return false
		}
	}
	if t.Flags.UnknownArtist {
		return true
	}
	for _, a := range t.Artists {
		if a.Role == RolePrimary {
			return true
		}
	}
	return false
}
