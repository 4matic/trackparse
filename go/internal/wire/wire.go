// Package wire is the JSON door into trackparse: every public function called with loose JSON
// arguments, returning JSON. The shared fixtures, the cross-port diff (scripts/compare-ports.mjs
// through cmd/dump) and fuzzers use it. Options arrive as loose JSON and are resolved with the
// same fallbacks as the JavaScript reference (a bad value means the default, SPEC R0.8), which
// the typed Go API cannot express.
//
// The package is internal so loose JSON never becomes public API. It holds only a hook: the
// root package trackparse fills it in when it is initialized, so a program must import
// trackparse (a blank import is enough) before calling it.
package wire

// Call runs one case. line is a JSON object
// {"fn", "input", "options", "formatOptions", "edits"} as in the shared fixtures (fn defaults
// to "parse", options and formatOptions to {}, edits to []); the result is the JSON text of
// the function's output, as the JavaScript package writes it. A line that is not a JSON object,
// or an unknown fn, gives null. Never panics.
//
// Nil until the trackparse package is initialized.
var Call func(line []byte) []byte
