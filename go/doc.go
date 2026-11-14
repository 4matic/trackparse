// Package trackparse parses music track strings ("Artist feat. Guest - Title (Remix)") and
// whole tracklists into structured data: artists with roles, the title, versions and remixes,
// junk such as "[Official Video]", positions and timestamps. Everything it recognizes goes into
// a field, and anything it does not recognize stays in the title, word for word.
//
//	track := trackparse.Parse(
//		"01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
//	)
//	track.Artists[1].Name, track.Artists[1].Role // "Becky Hill", RoleFeatured
//	track.Title                                  // "Afterglow"
//	track.Versions[0].Type                       // VersionTypeRemix
//	track.Versions[0].Artists[0].Name            // "Sub Focus"
//	track.Junk[0].Raw, track.Junk[0].Kind        // "Official Video", JunkKindVideo
//	track.Position.Number                        // 1
//
// # Functions
//
// [Parse] parses one track string, [ParseArtists] a bare artist credit, [Format] renders a
// track back to a string, [Normalize] is the Unicode cleanup every function runs first, and
// [AllArtists] lists the credits together with the remixers. [ParseTracklist] reads a whole
// tracklist, one entry per line, and checks its times; [ApplyEdits] rewrites only the lines its
// edits name. The functions that take options have a With form ([ParseWith], [FormatWith],
// [ParseTracklistWith], ...); the zero value of every options struct is the default. A
// [Parser] compiles parse options once for many calls.
//
// # Results and JSON
//
// Fields that are null in the spec's JSON are pointers, nil when absent, and slices in a result
// are never nil. Version types, junk kinds, warnings and the other names are typed string
// constants whose value is the spec's name; a function per type, such as [AllVersionTypes],
// lists them. Every result type has a MarshalJSON method that returns the bytes JSON.stringify
// writes in the JavaScript package (json.Marshal then escapes <, > and &; see the README), and
// [Track.UnmarshalJSON] reads that form back.
//
// # Errors and concurrency
//
// Nothing panics, and no parsing or editing function returns an error: any string gives a
// complete result. Invalid UTF-8 is read as the WHATWG decoder reads it, each maximal
// ill-formed subsequence becoming one U+FFFD, so every output string is valid UTF-8. An edit
// that cannot be applied is reported as an [EditWarningCodeEditInvalid] warning. The package
// functions and a [Parser] are safe for concurrent use.
//
// # Spec and other languages
//
// This is the Go port of a spec-driven parser. The JavaScript (npm), Python (PyPI), Rust
// (crates.io) and Go packages implement one spec, pass the same shared fixtures, give the same
// output on every fixture input, and are released together under one version number
// ([SpecVersion]). Comments in the source cite the spec's rule IDs (R0 to R13).
//
//   - Documentation, with every example in all four languages: https://4matic.github.io/trackparse/
//   - Playground: https://4matic.github.io/trackparse/playground/
//   - Spec: https://github.com/4matic/trackparse/blob/main/spec/SPEC.md
//   - Repository and README: https://github.com/4matic/trackparse/tree/main/go
package trackparse
