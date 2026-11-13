package trackparse

// TEMPORARY: stub bodies for tracklists (R13), so the module compiles and the fixture harness
// reads about 0%. The tracklist port replaces them with the real ones in the files named below
// and then deletes this file.

// parseTracklist is R13.1 to R13.8 on one (already sanitized) text. Real version: tl_parse.go
// (js/src/tracklist/parse.ts), using engine.probe for the two-pass list context.
func parseTracklist(text string, o resolvedTracklist) Tracklist {
	_, _ = text, o
	return Tracklist{SpecVersion: SpecVersion, Entries: []Entry{}, Warnings: []TracklistWarning{}}
}

// applyEdits is R13.9 on one (already sanitized) text. Edits are loose JSON objects read with
// the JS number rules (jsNumber: a bool is not a number; null, NaN and infinities make the edit
// edit-invalid); typed edits arrive in the same form. Real version: tl_edits.go
// (js/src/tracklist/edits.ts, rust/src/tracklist/edits.rs).
func applyEdits(text string, edits []jsonValue) rawEditResult {
	_ = edits
	return rawEditResult{text: text, changed: []int{}}
}
