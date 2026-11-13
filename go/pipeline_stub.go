package trackparse

// TEMPORARY: stub bodies for the artist-side output functions, so the module compiles. The
// artist-side port replaces each function with the real one in the file named in its comment
// and then deletes this file. The signatures are the contract: the public API in trackparse.go
// and the JSON door in wire.go call them.

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
