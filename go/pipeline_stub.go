package trackparse

// TEMPORARY: stub bodies for the single-track pipeline, so the module compiles and the
// fixture harness reads about 0%. The pipeline port replaces each function with the real one
// in the file named in its comment and then deletes this file. The signatures are the
// contract: the public API in trackparse.go and the JSON door in wire.go call them.

// run is R1 to R9 on one (already sanitized) input; listContext is the R13.4.2 flag that only
// matters in tracklist mode (R6.10). Real version: parser.go (js/src/parser.ts run/finish).
func (e *engine) run(input string, listContext bool) Track {
	_ = listContext
	mode := ModeClean
	if e.opts.mode.kind == requestMode {
		mode = e.opts.mode.mode
	}
	return Track{
		Input:    input,
		Mode:     mode,
		Artists:  []Artist{},
		Versions: []Version{},
		Junk:     []Junk{},
		Warnings: []Warning{},
	}
}

// runArtists is R10 on one (already sanitized) input. Real version: parser.go.
func (e *engine) runArtists(input string) []Artist {
	_ = input
	return []Artist{}
}

// normalize is R1. Real version: normalize.go.
func normalize(input string) string {
	return input
}

// formatTrack is R11. Real version: format.go.
func formatTrack(t *Track, o resolvedFormat) string {
	_ = o
	return t.Title
}

// allArtists is the artists of the track plus its version credits, deduplicated. Real version:
// format.go.
func allArtists(t *Track) []Artist {
	return append([]Artist{}, t.Artists...)
}

// Helpers only the real pipeline calls, referenced here so the linters accept the stub build.
var _ = []any{isLetterBefore, (*ctx).warn, matchKnownAt, isKnownArtist, isUnknownToken}

var _ = ctx{}.listSeq
