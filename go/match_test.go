package trackparse

// The fixture matcher of spec/fixtures/README.md (partial and exact), over the module's own
// JSON values. Same rules as js/test/match.ts, python/tests/match.py and rust/tests/fixtures.rs.

import (
	"slices"
	"strconv"
	"testing"
)

// show is a value as compact JSON, for messages.
func show(v jsonValue) string { return string(marshal(v)) }

// sameScalar compares scalars as JS does: numbers by value (7 equals 7.0), strings exactly.
func sameScalar(e, a jsonValue) bool {
	if e.kind != a.kind {
		return false
	}
	switch e.kind {
	case jsonNull:
		return true
	case jsonBool:
		return e.b == a.b
	case jsonNumber:
		return e.num == a.num
	case jsonString:
		return e.str == a.str
	}
	return false
}

// matchPartial is match: "partial" (README rules 1 to 4); "" on a match.
func matchPartial(expected, actual jsonValue, path string) string {
	switch expected.kind {
	case jsonArray:
		if actual.kind != jsonArray {
			return path + ": expected an array, got " + show(actual)
		}
		if len(actual.arr) != len(expected.arr) {
			return path + ": expected " + strconv.Itoa(len(expected.arr)) + " items, got " +
				strconv.Itoa(len(actual.arr)) + ": " + show(actual)
		}
		for i, ev := range expected.arr {
			av := actual.arr[i]
			p := path + "[" + strconv.Itoa(i) + "]"
			// Rule 4: artist shorthand.
			if ev.kind == jsonString && av.kind == jsonObject {
				if name, ok := av.get("name"); ok {
					if !sameScalar(ev, name) {
						return p + ".name: expected " + show(ev) + ", got " + show(name)
					}
					continue
				}
			}
			if d := matchPartial(ev, av, p); d != "" {
				return d
			}
		}
		return ""
	case jsonObject:
		if actual.kind != jsonObject {
			return path + ": expected an object, got " + show(actual)
		}
		for _, m := range expected.obj {
			av, ok := actual.get(m.key)
			if !ok {
				return path + "." + m.key + ": missing"
			}
			if d := matchPartial(m.val, av, path+"."+m.key); d != "" {
				return d
			}
		}
		return ""
	}
	if !sameScalar(expected, actual) {
		return path + ": expected " + show(expected) + ", got " + show(actual)
	}
	return ""
}

func sortedKeys(v jsonValue) []string {
	keys := make([]string, len(v.obj))
	for i, m := range v.obj {
		keys[i] = m.key
	}
	slices.Sort(keys)
	return keys
}

// matchExact is deep equality of complete values (README rule 5); "" on a match.
func matchExact(expected, actual jsonValue, path string) string {
	switch {
	case expected.kind == jsonArray && actual.kind == jsonArray && len(expected.arr) == len(actual.arr):
		for i := range expected.arr {
			if d := matchExact(expected.arr[i], actual.arr[i], path+"["+strconv.Itoa(i)+"]"); d != "" {
				return d
			}
		}
		return ""
	case expected.kind == jsonObject && actual.kind == jsonObject:
		ek, ak := sortedKeys(expected), sortedKeys(actual)
		if !slices.Equal(ek, ak) {
			return path + ": expected keys " + show(stringsValue(ek)) + ", got " + show(stringsValue(ak))
		}
		for _, k := range ek {
			ev, _ := expected.get(k)
			av, _ := actual.get(k)
			if d := matchExact(ev, av, path+"."+k); d != "" {
				return d
			}
		}
		return ""
	case expected.kind == jsonArray || expected.kind == jsonObject:
		return path + ": expected " + show(expected) + ", got " + show(actual)
	}
	if !sameScalar(expected, actual) {
		return path + ": expected " + show(expected) + ", got " + show(actual)
	}
	return ""
}

func stringsValue(list []string) jsonValue {
	out := make([]jsonValue, len(list))
	for i, s := range list {
		out[i] = jStr(s)
	}
	return jArr(out)
}

// withoutSpecVersion drops a top-level specVersion (README rule 6: never compared).
func withoutSpecVersion(v jsonValue) jsonValue {
	if v.kind != jsonObject {
		return v
	}
	out := jsonValue{kind: jsonObject}
	for _, m := range v.obj {
		if m.key != "specVersion" {
			out.obj = append(out.obj, m)
		}
	}
	return out
}

// matches is the fixture matcher; "" on a match.
func matches(expected, actual jsonValue, exact bool) string {
	e, a := withoutSpecVersion(expected), withoutSpecVersion(actual)
	if exact {
		return matchExact(e, a, "$")
	}
	return matchPartial(e, a, "$")
}

func mustJSON(t *testing.T, text string) jsonValue {
	t.Helper()
	v, err := parseJSON([]byte(text))
	if err != nil {
		t.Fatalf("bad JSON %s: %v", text, err)
	}
	return v
}

func TestMatcher(t *testing.T) {
	cases := []struct {
		expected, actual string
		exact, ok        bool
	}{
		// Rule 1: extra keys are ignored in partial mode; an explicit null must be null.
		{`{"title":"A"}`, `{"title":"A","year":null}`, false, true},
		{`{"year":null}`, `{"year":2015}`, false, false},
		{`{"year":null}`, `{}`, false, false},
		// Rule 2: arrays match in length and order.
		{`{"a":[1,2]}`, `{"a":[1,2,3]}`, false, false},
		{`{"a":[2,1]}`, `{"a":[1,2]}`, false, false},
		// Rule 3: scalars strictly; numbers by value.
		{`7`, `7.0`, false, true},
		{`"a"`, `"A"`, false, false},
		{`true`, `1`, false, false},
		// Rule 4: artist shorthand.
		{`["Noisia",{"name":"Foreign Beggars","role":"featured"}]`,
			`[{"name":"Noisia","role":"primary"},{"name":"Foreign Beggars","role":"featured"}]`, false, true},
		{`["Noisia"]`, `[{"name":"Phace"}]`, false, false},
		{`["Noisia"]`, `["Noisia"]`, false, true},
		// Rule 5: exact.
		{`{"a":1}`, `{"a":1,"b":2}`, true, false},
		{`{"b":2,"a":1}`, `{"a":1,"b":2}`, true, true},
		{`["Noisia"]`, `[{"name":"Noisia"}]`, true, false},
		// Rule 6: a top-level specVersion is never compared.
		{`{"specVersion":"9","x":1}`, `{"specVersion":"0.1","x":1}`, true, true},
	}
	for _, c := range cases {
		d := matches(mustJSON(t, c.expected), mustJSON(t, c.actual), c.exact)
		if (d == "") != c.ok {
			t.Errorf("matches(%s, %s, exact=%v) = %q, want ok=%v", c.expected, c.actual, c.exact, d, c.ok)
		}
	}
}
