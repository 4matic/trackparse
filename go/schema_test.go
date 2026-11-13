package trackparse

// A small JSON Schema validator for the subset spec/schema uses (type, enum, required,
// properties, additionalProperties: false, items, oneOf, $ref within and across the schema
// files, minimum, maximum, minLength). The module has no dependencies, so no general validator;
// a keyword outside the subset fails the run, so a schema change cannot be skipped silently.

import (
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"unicode/utf8"
)

// annotation keywords that do not constrain a value.
var schemaAnnotations = map[string]bool{
	"$schema": true, "$id": true, "title": true, "description": true, "default": true,
	"$comment": true, "$defs": true, "examples": true,
}

// schemaSet holds the schema files of spec/schema by file name.
type schemaSet struct {
	docs map[string]jsonValue
}

func loadSchemas(dir string) (*schemaSet, error) {
	s := &schemaSet{docs: map[string]jsonValue{}}
	for _, name := range []string{"parsed-track.schema.json", "tracklist.schema.json"} {
		text, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil {
			return nil, err
		}
		v, err := parseJSON(text)
		if err != nil {
			return nil, err
		}
		s.docs[name] = v
	}
	return s, nil
}

// resolve follows a $ref from file: "#/$defs/x", "other.json" or "other.json#/$defs/x".
func (s *schemaSet) resolve(file, ref string) (jsonValue, string, bool) {
	target, pointer, _ := strings.Cut(ref, "#")
	if target != "" {
		file = target
	}
	node, ok := s.docs[file]
	if !ok {
		return jsonValue{}, "", false
	}
	for _, part := range strings.Split(strings.TrimPrefix(pointer, "/"), "/") {
		if part == "" {
			continue
		}
		if node, ok = node.get(part); !ok {
			return jsonValue{}, "", false
		}
	}
	return node, file, true
}

// validateAt checks v against the definition at ref of file ("" for the root); "" when valid.
func (s *schemaSet) validateAt(file, ref string, v jsonValue) string {
	schema, f, ok := s.resolve(file, ref)
	if !ok {
		return "schema: cannot resolve " + file + ref
	}
	return s.validate(schema, f, v, "$")
}

func typeMatches(name string, v jsonValue) bool {
	switch name {
	case "null":
		return v.kind == jsonNull
	case "boolean":
		return v.kind == jsonBool
	case "string":
		return v.kind == jsonString
	case "number":
		return v.kind == jsonNumber && !math.IsNaN(v.num) && !math.IsInf(v.num, 0)
	case "integer":
		return v.kind == jsonNumber && v.num == math.Trunc(v.num) && !math.IsInf(v.num, 0)
	case "array":
		return v.kind == jsonArray
	case "object":
		return v.kind == jsonObject
	}
	return false
}

func (s *schemaSet) validate(schema jsonValue, file string, v jsonValue, path string) string {
	if schema.kind != jsonObject {
		return path + ": schema is not an object"
	}
	for _, kw := range schema.obj {
		if schemaAnnotations[kw.key] {
			continue
		}
		if d := s.keyword(kw.key, kw.val, schema, file, v, path); d != "" {
			return d
		}
	}
	return ""
}

func (s *schemaSet) keyword(key string, arg, schema jsonValue, file string, v jsonValue, path string) string {
	switch key {
	case "$ref":
		target, f, ok := s.resolve(file, arg.str)
		if !ok {
			return path + ": cannot resolve $ref " + arg.str
		}
		return s.validate(target, f, v, path)
	case "type":
		names := []jsonValue{arg}
		if arg.kind == jsonArray {
			names = arg.arr
		}
		for _, n := range names {
			if typeMatches(n.str, v) {
				return ""
			}
		}
		return path + ": expected type " + show(arg) + ", got " + show(v)
	case "enum":
		for _, e := range arg.arr {
			if sameScalar(e, v) {
				return ""
			}
		}
		return path + ": " + show(v) + " is not one of " + show(arg)
	case "required":
		if v.kind != jsonObject {
			return ""
		}
		for _, k := range arg.arr {
			if _, ok := v.get(k.str); !ok {
				return path + ": missing required key " + k.str
			}
		}
	case "properties":
		if v.kind != jsonObject {
			return ""
		}
		for _, m := range v.obj {
			if sub, ok := arg.get(m.key); ok {
				if d := s.validate(sub, file, m.val, path+"."+m.key); d != "" {
					return d
				}
			}
		}
	case "additionalProperties":
		if v.kind != jsonObject {
			return ""
		}
		if arg.kind != jsonBool || arg.b {
			return path + ": unsupported additionalProperties " + show(arg)
		}
		props, _ := schema.get("properties")
		for _, m := range v.obj {
			if _, ok := props.get(m.key); !ok {
				return path + ": unexpected key " + m.key
			}
		}
	case "items":
		if v.kind != jsonArray {
			return ""
		}
		for i, x := range v.arr {
			if d := s.validate(arg, file, x, path+"["+strconv.Itoa(i)+"]"); d != "" {
				return d
			}
		}
	case "oneOf":
		n, last := 0, ""
		for _, alt := range arg.arr {
			if d := s.validate(alt, file, v, path); d == "" {
				n++
			} else {
				last = d
			}
		}
		if n != 1 {
			return path + ": " + strconv.Itoa(n) + " oneOf branches match (" + last + ")"
		}
	case "minimum":
		if v.kind == jsonNumber && v.num < arg.num {
			return path + ": " + show(v) + " is below " + show(arg)
		}
	case "maximum":
		if v.kind == jsonNumber && v.num > arg.num {
			return path + ": " + show(v) + " is above " + show(arg)
		}
	case "minLength":
		if v.kind == jsonString && float64(utf8.RuneCountInString(v.str)) < arg.num {
			return path + ": string shorter than " + show(arg)
		}
	default:
		return path + ": unsupported schema keyword " + key + " (extend schema_test.go)"
	}
	return ""
}
