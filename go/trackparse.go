package trackparse

// The public API. Every function is total (R0.8): any string gives a result, bad option values
// fall back to the defaults, and nothing panics. Input that is not valid UTF-8 is read with the
// WHATWG rule first (each maximal ill-formed subpart becomes one U+FFFD, see sanitize), so the
// Go and Rust ports read the same bytes as the same text.

// Parse parses one track string with the default options.
func Parse(input string) Track {
	return sharedEngine().run(sanitize(input), false)
}

// ParseWith parses one track string with options (R12). To parse many strings with the same
// options, build a [Parser] once instead: the options' tables are compiled per call here.
func ParseWith(input string, options ParseOptions) Track {
	return NewParser(options).Parse(input)
}

// ParseArtists splits an artist credit string into artists (R10), with the default options.
func ParseArtists(input string) []Artist {
	return sharedEngine().runArtists(sanitize(input))
}

// ParseArtistsWith is [ParseArtists] with options.
func ParseArtistsWith(input string, options ParseOptions) []Artist {
	return NewParser(options).ParseArtists(input)
}

// Format renders a parsed track back to a string (R11) with the default options: artists,
// " - ", the title and its versions, written as they were found.
func Format(track Track) string {
	return formatTrack(&track, resolveFormat(&FormatOptions{}))
}

// FormatWith is [Format] with options.
func FormatWith(track Track, options FormatOptions) string {
	return formatTrack(&track, resolveFormat(&options))
}

// Normalize is the R1 normalization the parser applies first: NFC, whitespace and dash
// variants, invisible characters.
func Normalize(input string) string {
	return normalize(sanitize(input))
}

// AllArtists is every artist of the track, the version credits (remixers) included, in the
// order written and without duplicates.
func AllArtists(track Track) []Artist {
	return allArtists(&track)
}

// Parser parses with fixed options, compiled once. It is safe for concurrent use. The zero
// Parser uses the default options.
type Parser struct {
	e *engine
}

// NewParser compiles options (R12) into a [Parser].
func NewParser(options ParseOptions) *Parser {
	return &Parser{e: newEngine(resolveParse(&options))}
}

func (p *Parser) engine() *engine {
	if p == nil || p.e == nil {
		return sharedEngine()
	}
	return p.e
}

// Parse parses one track string with the parser's options.
func (p *Parser) Parse(input string) Track {
	return p.engine().run(sanitize(input), false)
}

// ParseArtists splits an artist credit string with the parser's options (R10).
func (p *Parser) ParseArtists(input string) []Artist {
	return p.engine().runArtists(sanitize(input))
}

// ParseTracklist parses a multi-line tracklist with the default options (R13). The text is
// split at LF, CRLF and CR only; every line becomes one entry, and the entries' Raw and Eol,
// joined, are the text again (R13.1.2).
func ParseTracklist(text string) Tracklist {
	return parseTracklist(sanitize(text), resolveTracklist(&TracklistOptions{}))
}

// ParseTracklistWith is [ParseTracklist] with options (R13.10).
func ParseTracklistWith(text string, options TracklistOptions) Tracklist {
	return parseTracklist(sanitize(text), resolveTracklist(&options))
}

// ApplyEdits applies edits to a tracklist text (R13.9), in order, each to the result of the
// previous one. Lines no edit wrote stay byte-identical. An edit that cannot be applied changes
// nothing and is reported as [EditWarningCodeEditInvalid].
func ApplyEdits(text string, edits []Edit) EditResult {
	loose := make([]jsonValue, len(edits))
	for i := range edits {
		// Through the edit's JSON form, which also sanitizes its strings: typed and loose
		// edits share one code path, as in the Rust crate.
		loose[i] = jsonOf(&edits[i])
	}
	return applyEdits(sanitize(text), loose).result()
}

// ApplyEditsWith is [ApplyEdits] with options. The options are accepted for symmetry with the
// other ports (R13.10); edits do not depend on them today.
func ApplyEditsWith(text string, edits []Edit, options TracklistOptions) EditResult {
	_ = options
	return ApplyEdits(text, edits)
}
