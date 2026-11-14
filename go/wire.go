package trackparse

// The JSON door: fixture-style calls with loose JSON arguments (wireCall). The fixture tests call
// it directly; internal/cmd/dump reaches it through internal/wire, which wire_hook.go fills in
// only when the module is built with -tags trackparse_wire. Without the tag nothing refers to
// the door, so it is not linked into the programs that use the module.

// artistList writes a list of artists as JSON.
type artistList []Artist

func (l artistList) write(w *jw) { writeArtists(w, l) }

// wireString writes a JSON string.
type wireString string

func (s wireString) write(w *jw) { w.str(string(s)) }

// wireCall is one fixture-style call: fn is parse, parseArtists, format, normalize,
// parseTracklist or applyEdits; format parses input with options first, then formats with
// formatOptions. Unknown functions give null. input must be valid UTF-8 (the JSON reader
// guarantees it).
func wireCall(fn, input string, options, formatOptions, edits jsonValue) []byte {
	switch fn {
	case "parse":
		t := newEngine(resolveParseJSON(options)).run(input, false)
		return marshal(&t)
	case "parseArtists":
		return marshal(artistList(newEngine(resolveParseJSON(options)).runArtists(input)))
	case "format":
		t := newEngine(resolveParseJSON(options)).run(input, false)
		return marshal(wireString(formatTrack(&t, resolveFormatJSON(formatOptions))))
	case "normalize":
		return marshal(wireString(normalize(input)))
	case "parseTracklist":
		l := parseTracklist(input, resolveTracklistJSON(options))
		return marshal(&l)
	case "applyEdits":
		// Only an array has edits.
		var list []jsonValue
		if edits.kind == jsonArray {
			list = edits.arr
		}
		r := applyEdits(input, list)
		return marshal(&r)
	}
	return []byte("null")
}
