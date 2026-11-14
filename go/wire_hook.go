//go:build trackparse_wire

package trackparse

import "github.com/4matic/trackparse/go/internal/wire"

// Built only with -tags trackparse_wire, for internal/cmd/dump: this init is what links the JSON
// door into a binary.

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
