package trackparse

import "github.com/4matic/trackparse/go/internal/wire"

// The JSON door (internal/wire): fixture-style calls with loose JSON arguments.

func init() {
	wire.Call = wireLine
}

// wireLine runs one case object {"fn", "input", "options", "formatOptions", "edits"}.
func wireLine(line []byte) []byte {
	c, err := parseJSON(line)
	if err != nil || c.kind != jsonObject {
		return []byte("null")
	}
	fn, ok := c.getString("fn")
	if !ok {
		fn = "parse"
	}
	input, _ := c.getString("input")
	options, ok := c.get("options")
	if !ok {
		options = jsonValue{kind: jsonObject}
	}
	formatOptions, ok := c.get("formatOptions")
	if !ok {
		formatOptions = jsonValue{kind: jsonObject}
	}
	edits, ok := c.get("edits")
	if !ok {
		edits = jArr(nil)
	}
	return wireCall(fn, input, options, formatOptions, edits)
}

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
