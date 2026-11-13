package trackparse

import "sync"

// Per-call parse context (resolved options plus vocabulary tables) and the engine that holds
// what is computed once per Parser.

// engine is resolved options with the tables and knownArtists keys built once (JS createParser
// and createListParser). It is read-only after newEngine, so it is safe for concurrent use;
// every call makes its own ctx.
type engine struct {
	opts      resolvedOptions
	t         *tables
	knownKeys wordSet
	// knownMaxLen is the longest knownArtists entry, in UTF-16 units.
	knownMaxLen int
}

// newEngine builds the tables and known keys for o.
func newEngine(o resolvedOptions) *engine {
	keys, maxLen := knownKeysOf(o.knownArtists)
	return &engine{opts: o, t: tablesFor(o.keywords), knownKeys: keys, knownMaxLen: maxLen}
}

// sharedEngine is the engine behind Parse and ParseArtists without options.
var sharedEngine = sync.OnceValue(func() *engine { return newEngine(resolvedOptions{}) })

// ctx is the state of one parse.
type ctx struct {
	t         *tables
	knownKeys wordSet
	// knownMaxLen is the longest knownArtists entry, in UTF-16 units (bounds the R7.3a search).
	knownMaxLen int
	splitAnd    splitAndMode
	warnings    []Warning
	// listSeq gives every split credit list an id (R7.6 joiner inheritance across R9.2).
	listSeq int
}

// newCtx is a fresh per-call context.
func (e *engine) newCtx() *ctx {
	return &ctx{t: e.t, knownKeys: e.knownKeys, knownMaxLen: e.knownMaxLen, splitAnd: e.opts.splitAnd}
}

// knownKeysOf is the knownArtists part of a context: the dedup keys and the longest entry in
// UTF-16 units.
func knownKeysOf(known []string) (wordSet, int) {
	keys := wordSet{}
	maxLen := 0
	for _, k := range known {
		if key := dedupKey(k); key != "" {
			keys[key] = struct{}{}
		}
		if n := utf16LenString(k); n > maxLen {
			maxLen = n
		}
	}
	return keys, maxLen
}

// warn adds w once.
func (c *ctx) warn(w Warning) {
	for _, x := range c.warnings {
		if x == w {
			return
		}
	}
	c.warnings = append(c.warnings, w)
}

// matchKnownAt is R7.3a: the length (in code points) of the longest knownArtists entry
// matching text at start (dedup-key comparison), followed by the end, whitespace, "," or ";"
// (or extra, unless it is noRune: R4.2 also accepts "-", so 3 Doors Down-Kryptonite is
// protected). 0 when none matches. The search window is 2 * longest + 8 UTF-16 units, exactly
// as in the JS reference.
func matchKnownAt(text []rune, start int, c *ctx, extra rune) int {
	if len(c.knownKeys) == 0 || start < 0 {
		return 0
	}
	budget := c.knownMaxLen*2 + 8
	limit, used := start, 0
	for limit < len(text) {
		w := 1
		if text[limit] >= 0x10000 {
			w = 2
		}
		if used+w > budget {
			break
		}
		used += w
		limit++
	}
	for end := limit; end > start; end-- {
		next := at(text, end)
		boundary := end == len(text) || next == ',' || next == ';' || isWhitespace(next) ||
			(extra != noRune && next == extra)
		if boundary && c.knownKeys.has(dedupKey(string(sub(text, start, end)))) {
			return end - start
		}
	}
	return 0
}

// isKnownArtist reports whether text is a knownArtists entry (dedup-key comparison).
func isKnownArtist(text string, c *ctx) bool {
	return len(c.knownKeys) > 0 && c.knownKeys.has(dedupKey(text))
}
