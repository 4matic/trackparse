// Package trackparse parses music track strings ("Artist feat. Guest - Title (Remix)") and
// whole tracklists into structured data: artists with roles, the title, versions and remixes,
// junk such as "[Official Video]", positions and timestamps.
//
// It is the Go port of a spec-driven parser. The JavaScript, Python, Rust and Go packages pass
// the same shared fixtures and give the same output on every fixture input. Comments in the
// source cite the spec's rule IDs (R0 to R13); the spec is at
// https://github.com/4matic/trackparse/blob/main/spec/SPEC.md and the documentation at
// https://4matic.github.io/trackparse/.
package trackparse
