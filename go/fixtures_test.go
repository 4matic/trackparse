package trackparse

// Runs every shared fixture in ../spec/fixtures through the JSON door (wireCall), as
// rust/tests/fixtures.rs does.
//
//   - Matching follows spec/fixtures/README.md (match_test.go); status pass / xfail / ambiguous.
//   - Every output is validated against spec/schema (schema_test.go).
//   - testdata/skip.json lists port-specific known failures by case id; a listed case that
//     passes fails the run, so the list can only shrink.
//   - TRACKPARSE_REPORT=1 prints the pass rate per file and per rule instead of failing (the
//     conformance report): TRACKPARSE_REPORT=1 go test -run TestFixtures -v
//   - TRACKPARSE_ONLY=<text> runs the cases whose file name or id contains the text.
//   - TRACKPARSE_ALLOW_FAIL=1 logs the failures but keeps the test green (a local opt-in for
//     work in progress; CI never sets it, so by default any failing case fails the test).
//
// The test is skipped when ../spec is absent (the module zip has no spec).

import (
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

const specDir = "../spec"

// fixtureFiles lists the .json files under dir, sorted, recursively.
func fixtureFiles(t *testing.T, dir string) []string {
	t.Helper()
	var out []string
	err := filepath.WalkDir(dir, func(path string, d os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() && strings.HasSuffix(path, ".json") {
			out = append(out, path)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	slices.Sort(out)
	return out
}

func readJSONFile(t *testing.T, path string) jsonValue {
	t.Helper()
	text, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	v, err := parseJSON(text)
	if err != nil {
		t.Fatalf("%s: %v", path, err)
	}
	return v
}

// merged is {...defaults[key], ...c[key]} (shallow).
func merged(defaults, c jsonValue, key string) jsonValue {
	out := jsonValue{kind: jsonObject}
	for _, src := range []jsonValue{defaults, c} {
		v, ok := src.get(key)
		if !ok || v.kind != jsonObject {
			continue
		}
		for _, m := range v.obj {
			replaced := false
			for i := range out.obj {
				if out.obj[i].key == m.key {
					out.obj[i].val = m.val
					replaced = true
				}
			}
			if !replaced {
				out.obj = append(out.obj, m)
			}
		}
	}
	return out
}

// checkSchema validates one output by fn; "" when valid.
func checkSchema(s *schemaSet, fn string, out jsonValue) string {
	switch fn {
	case "parse":
		return s.validateAt("parsed-track.schema.json", "", out)
	case "parseArtists":
		if out.kind != jsonArray {
			return "parseArtists: not an array"
		}
		for _, a := range out.arr {
			if d := s.validateAt("parsed-track.schema.json", "#/$defs/artist", a); d != "" {
				return d
			}
		}
	case "format", "normalize":
		if out.kind != jsonString {
			return fn + ": not a string"
		}
	case "parseTracklist":
		return s.validateAt("tracklist.schema.json", "", out)
	case "applyEdits":
		return s.validateAt("tracklist.schema.json", "#/$defs/editResult", out)
	}
	return ""
}

// runFixture runs one case; "" when it behaves as its status demands.
func runFixture(s *schemaSet, file, c jsonValue) (problem string) {
	defaults, _ := file.get("defaults")
	fn, ok := c.getString("fn")
	if !ok {
		if fn, ok = defaults.getString("fn"); !ok {
			fn = "parse"
		}
	}
	input, _ := c.getString("input")
	edits, ok := c.get("edits")
	if !ok {
		edits = jArr(nil)
	}
	var out []byte
	func() {
		defer func() {
			if r := recover(); r != nil {
				problem = fmt.Sprintf("panicked: %v", r)
			}
		}()
		out = wireCall(fn, input, merged(defaults, c, "options"), merged(defaults, c, "formatOptions"), edits)
	}()
	if problem != "" {
		return problem
	}
	actual, err := parseJSON(out)
	if err != nil {
		return "the module wrote invalid JSON: " + string(out)
	}
	if d := checkSchema(s, fn, actual); d != "" {
		return "output violates the schema: " + d
	}

	status, _ := c.getString("status")
	matchMode, _ := c.getString("match")
	exact := matchMode == "exact"
	var mismatch string
	if status == "ambiguous" {
		alts, _ := c.get("expectedAny")
		var diffs []string
		for _, e := range alts.arr {
			d := matches(e, actual, exact)
			if d == "" {
				diffs = nil
				break
			}
			diffs = append(diffs, d)
		}
		if len(diffs) > 0 || len(alts.arr) == 0 {
			mismatch = "no expectedAny entry matched: " + strings.Join(diffs, "; ")
		}
	} else {
		expected, _ := c.get("expected")
		mismatch = matches(expected, actual, exact)
	}
	if status == "xfail" {
		if mismatch == "" {
			return "xfail case unexpectedly passes"
		}
		return ""
	}
	return mismatch
}

type ruleCount struct{ total, ok int }

func TestFixtures(t *testing.T) {
	fixturesDir := filepath.Join(specDir, "fixtures")
	if _, err := os.Stat(fixturesDir); err != nil {
		t.Skip("../spec/fixtures not found (the module zip has no spec)")
	}
	schemas, err := loadSchemas(filepath.Join(specDir, "schema"))
	if err != nil {
		t.Fatal(err)
	}
	report := os.Getenv("TRACKPARSE_REPORT") != ""
	allowFail := os.Getenv("TRACKPARSE_ALLOW_FAIL") != ""
	only := os.Getenv("TRACKPARSE_ONLY")
	skip := readJSONFile(t, filepath.Join("testdata", "skip.json"))
	if skip.kind != jsonObject {
		t.Fatal("testdata/skip.json is not an object")
	}

	var failures []string
	type fileCount struct {
		name      string
		ok, total int
	}
	var perFile []fileCount
	perRule := map[string]*ruleCount{}
	total, passed := 0, 0

	for _, path := range fixtureFiles(t, fixturesDir) {
		data := readJSONFile(t, path)
		cases, ok := data.get("cases")
		if !ok || cases.kind != jsonArray {
			continue // metadata such as generated/summary.json
		}
		name, _ := filepath.Rel(fixturesDir, path)
		name = filepath.ToSlash(name)
		n, good := 0, 0
		for _, c := range cases.arr {
			id, _ := c.getString("id")
			if only != "" && !strings.Contains(name, only) && !strings.Contains(id, only) {
				continue
			}
			problem := runFixture(schemas, data, c)
			if reason, listed := skip.getString(id); listed {
				if problem == "" {
					problem = "skip-listed (" + reason + ") but now passes: remove it from testdata/skip.json"
				} else {
					problem = ""
				}
			}
			n++
			if problem == "" {
				good++
			}
			rules, _ := c.get("rules")
			for _, r := range rules.arr {
				rc := perRule[r.str]
				if rc == nil {
					rc = &ruleCount{}
					perRule[r.str] = rc
				}
				rc.total++
				if problem == "" {
					rc.ok++
				}
			}
			if problem != "" && len(failures) < 40 {
				in, _ := c.get("input")
				failures = append(failures, fmt.Sprintf("%s\n  input: %s\n  %s", id, show(in), problem))
			}
		}
		total += n
		passed += good
		perFile = append(perFile, fileCount{name, good, n})
	}

	if report {
		var b strings.Builder
		fmt.Fprintf(&b, "\nconformance: %d / %d\n", passed, total)
		for _, f := range perFile {
			mark := ""
			if f.ok != f.total {
				mark = "  <--"
			}
			fmt.Fprintf(&b, "  %5d / %-5d %s%s\n", f.ok, f.total, f.name, mark)
		}
		b.WriteString("by rule (failing only):\n")
		rules := make([]string, 0, len(perRule))
		for r := range perRule {
			rules = append(rules, r)
		}
		slices.Sort(rules)
		for _, r := range rules {
			if rc := perRule[r]; rc.ok != rc.total {
				fmt.Fprintf(&b, "  %-10s %d / %d\n", r, rc.ok, rc.total)
			}
		}
		for i, f := range failures {
			if i == 15 {
				break
			}
			fmt.Fprintf(&b, "\n%s\n", f)
		}
		t.Log(b.String())
		return
	}
	if total == 0 {
		t.Fatal("no fixtures found")
	}
	if passed != total {
		msg := fmt.Sprintf("%d of %d fixture cases fail; first ones:\n\n%s", total-passed, total,
			strings.Join(failures, "\n\n"))
		if allowFail {
			t.Logf("TRACKPARSE_ALLOW_FAIL: %d / %d pass", passed, total)
			return
		}
		t.Fatal(msg)
	}
}
