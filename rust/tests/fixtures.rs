//! Runs every shared fixture in `spec/fixtures` through `trackparse::wire::call`.
//!
//! - Matching follows spec/fixtures/README.md (partial / exact, artist shorthand, `specVersion`
//!   never compared); status `pass` / `xfail` / `ambiguous`.
//! - `tests/skip.json` lists port-specific known failures; a listed case that passes fails the
//!   run, so the list can only shrink.
//! - `TRACKPARSE_REPORT=1 cargo test --test fixtures -- --nocapture` prints the pass rate per file
//!   and per rule instead of failing (the conformance report).
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::indexing_slicing)]

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde_json::{Map, Value};
use trackparse::json::Json;

fn spec_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../spec")
}

fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
    let mut entries: Vec<PathBuf> =
        std::fs::read_dir(dir).unwrap().map(|e| e.unwrap().path()).collect();
    entries.sort();
    for p in entries {
        if p.is_dir() {
            walk(&p, out);
        } else if p.extension().is_some_and(|e| e == "json") {
            out.push(p);
        }
    }
}

fn show(v: &Value) -> String {
    serde_json::to_string(v).unwrap()
}

/// `match: "partial"` (README rules 1–4). `None` on a match.
fn match_partial(expected: &Value, actual: &Value, path: &str) -> Option<String> {
    match expected {
        Value::Array(e) => {
            let Value::Array(a) = actual else {
                return Some(format!("{path}: expected an array, got {}", show(actual)));
            };
            if a.len() != e.len() {
                return Some(format!(
                    "{path}: expected {} items, got {}: {}",
                    e.len(),
                    a.len(),
                    show(actual)
                ));
            }
            for (i, (ev, av)) in e.iter().zip(a).enumerate() {
                // Rule 4: artist shorthand.
                if let (Value::String(name), Value::Object(o)) = (ev, av) {
                    if let Some(actual_name) = o.get("name") {
                        if actual_name != ev {
                            return Some(format!(
                                "{path}[{i}].name: expected {name:?}, got {}",
                                show(actual_name)
                            ));
                        }
                        continue;
                    }
                }
                if let Some(d) = match_partial(ev, av, &format!("{path}[{i}]")) {
                    return Some(d);
                }
            }
            None
        }
        Value::Object(e) => {
            let Value::Object(a) = actual else {
                return Some(format!("{path}: expected an object, got {}", show(actual)));
            };
            for (k, ev) in e {
                let Some(av) = a.get(k) else { return Some(format!("{path}.{k}: missing")) };
                if let Some(d) = match_partial(ev, av, &format!("{path}.{k}")) {
                    return Some(d);
                }
            }
            None
        }
        _ => (!same(expected, actual))
            .then(|| format!("{path}: expected {}, got {}", show(expected), show(actual))),
    }
}

/// Scalars compare as in JS: numbers by value (`7` equals `7.0`).
fn same(e: &Value, a: &Value) -> bool {
    match (e, a) {
        (Value::Number(x), Value::Number(y)) => x.as_f64() == y.as_f64(),
        _ => e == a,
    }
}

/// Deep equality of complete values (README rule 5).
fn match_exact(expected: &Value, actual: &Value, path: &str) -> Option<String> {
    match (expected, actual) {
        (Value::Array(e), Value::Array(a)) if e.len() == a.len() => e
            .iter()
            .zip(a)
            .enumerate()
            .find_map(|(i, (ev, av))| match_exact(ev, av, &format!("{path}[{i}]"))),
        (Value::Object(e), Value::Object(a)) => {
            let mut ek: Vec<&String> = e.keys().collect();
            let mut ak: Vec<&String> = a.keys().collect();
            ek.sort();
            ak.sort();
            if ek != ak {
                return Some(format!("{path}: expected keys {ek:?}, got {ak:?}"));
            }
            ek.into_iter().find_map(|k| match_exact(&e[k], &a[k], &format!("{path}.{k}")))
        }
        (Value::Array(_) | Value::Object(_), _) => {
            Some(format!("{path}: expected {}, got {}", show(expected), show(actual)))
        }
        _ => (!same(expected, actual))
            .then(|| format!("{path}: expected {}, got {}", show(expected), show(actual))),
    }
}

/// README rule 6: a top-level `specVersion` is never compared.
fn without_spec_version(v: &Value) -> Value {
    match v {
        Value::Object(o) if o.contains_key("specVersion") => {
            let mut o = o.clone();
            o.remove("specVersion");
            Value::Object(o)
        }
        _ => v.clone(),
    }
}

fn matches(expected: &Value, actual: &Value, exact: bool) -> Option<String> {
    let (e, a) = (without_spec_version(expected), without_spec_version(actual));
    if exact {
        match_exact(&e, &a, "$")
    } else {
        match_partial(&e, &a, "$")
    }
}

/// serde_json → the crate's own JSON (through text, which also exercises `Json::parse`).
/// Non-finite numbers travel as marker strings (see `load`) and become `1e999` again here, which
/// `Json::parse` reads as infinity, as JS does.
fn to_crate_json(v: &Value) -> Json {
    let text = serde_json::to_string(v)
        .unwrap()
        .replace(&format!("\"{NEG_INFINITY}\""), "-1e999")
        .replace(&format!("\"{INFINITY}\""), "1e999");
    Json::parse(&text).expect("round trip")
}

fn merged(defaults: Option<&Value>, own: Option<&Value>) -> Value {
    let mut out = Map::new();
    for src in [defaults, own].into_iter().flatten() {
        if let Value::Object(o) = src {
            for (k, v) in o {
                out.insert(k.clone(), v.clone());
            }
        }
    }
    Value::Object(out)
}

struct Outcome {
    /// `None` when the case behaves as its status demands.
    failure: Option<String>,
}

fn run_case(file: &Value, case: &Value) -> Outcome {
    let defaults = file.get("defaults");
    let func = case
        .get("fn")
        .or_else(|| defaults.and_then(|d| d.get("fn")))
        .and_then(Value::as_str)
        .unwrap_or("parse");
    let options = merged(defaults.and_then(|d| d.get("options")), case.get("options"));
    let format_options =
        merged(defaults.and_then(|d| d.get("formatOptions")), case.get("formatOptions"));
    let edits = case.get("edits").cloned().unwrap_or(Value::Array(vec![]));
    let input = case.get("input").and_then(Value::as_str).unwrap_or("");

    let result = std::panic::catch_unwind(|| {
        trackparse::wire::call(
            func,
            input,
            &to_crate_json(&options),
            &to_crate_json(&format_options),
            &to_crate_json(&edits),
        )
    });
    let actual: Value = match result {
        Ok(json) => serde_json::from_str(&json.write()).expect("the crate wrote invalid JSON"),
        Err(_) => return Outcome { failure: Some("panicked".into()) },
    };

    let status = case.get("status").and_then(Value::as_str).unwrap_or("pass");
    let exact = case.get("match").and_then(Value::as_str) == Some("exact");
    let mismatch = if status == "ambiguous" {
        let any = case.get("expectedAny").and_then(Value::as_array).cloned().unwrap_or_default();
        let diffs: Vec<Option<String>> = any.iter().map(|e| matches(e, &actual, exact)).collect();
        if diffs.iter().any(Option::is_none) {
            None
        } else {
            Some(format!(
                "no expectedAny entry matched: {:?}",
                diffs.into_iter().flatten().collect::<Vec<_>>()
            ))
        }
    } else {
        matches(case.get("expected").unwrap_or(&Value::Null), &actual, exact)
    };
    let failure = if status == "xfail" {
        mismatch.is_none().then(|| "xfail case unexpectedly passes".to_string())
    } else {
        mismatch
    };
    Outcome { failure }
}

const INFINITY: &str = "\u{1}trackparse:+infinity";
const NEG_INFINITY: &str = "\u{1}trackparse:-infinity";

/// `1e999` is not a number serde_json reads; fixtures use it for infinity (R13.9.6). It is read
/// as a marker string (only where a value can stand, after `: `), which `to_crate_json` turns back into the number.
fn load(path: &Path) -> Value {
    let text = std::fs::read_to_string(path).unwrap();
    let marker = |m: &str| serde_json::to_string(m).unwrap();
    serde_json::from_str(
        &text
            .replace(": -1e999", &format!(": {}", marker(NEG_INFINITY)))
            .replace(": 1e999", &format!(": {}", marker(INFINITY))),
    )
    .unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

#[test]
fn shared_fixtures() {
    let report = std::env::var("TRACKPARSE_REPORT").is_ok();
    let only = std::env::var("TRACKPARSE_ONLY").ok(); // substring of the file name or case id
    let skip: BTreeMap<String, String> = serde_json::from_str(
        &std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/skip.json"))
            .unwrap(),
    )
    .unwrap();

    let mut files = Vec::new();
    walk(&spec_dir().join("fixtures"), &mut files);
    let mut failures: Vec<String> = Vec::new();
    let mut per_file: Vec<(String, usize, usize)> = Vec::new();
    let mut per_rule: BTreeMap<String, (usize, usize)> = BTreeMap::new();
    let (mut total, mut passed) = (0usize, 0usize);

    for path in &files {
        let data = load(path);
        let Some(cases) = data.get("cases").and_then(Value::as_array) else { continue };
        let name = path.strip_prefix(spec_dir().join("fixtures")).unwrap().display().to_string();
        let (mut n, mut ok) = (0usize, 0usize);
        for case in cases {
            let id = case.get("id").and_then(Value::as_str).unwrap_or("?");
            if let Some(f) = &only {
                if !name.contains(f.as_str()) && !id.contains(f.as_str()) {
                    continue;
                }
            }
            let mut problem = run_case(&data, case).failure;
            if let Some(reason) = skip.get(id) {
                problem = match problem {
                    None => Some(format!(
                        "skip-listed ({reason}) but now passes: remove it from tests/skip.json"
                    )),
                    Some(_) => None,
                };
            }
            n += 1;
            let good = problem.is_none();
            if good {
                ok += 1;
            }
            for rule in case
                .get("rules")
                .and_then(Value::as_array)
                .into_iter()
                .flatten()
                .filter_map(Value::as_str)
            {
                let e = per_rule.entry(rule.to_string()).or_default();
                e.0 += 1;
                e.1 += usize::from(good);
            }
            if let Some(p) = problem {
                if failures.len() < 40 {
                    failures.push(format!(
                        "{id}\n  input: {}\n  {p}",
                        case.get("input").map(show).unwrap_or_default()
                    ));
                }
            }
        }
        total += n;
        passed += ok;
        per_file.push((name, ok, n));
    }

    if report {
        println!("\nconformance: {passed} / {total}");
        for (name, ok, n) in &per_file {
            println!("  {:>5} / {:<5} {}{}", ok, n, name, if ok == n { "" } else { "  <--" });
        }
        println!("by rule (failing only):");
        for (rule, (n, ok)) in &per_rule {
            if ok != n {
                println!("  {rule:<10} {ok} / {n}");
            }
        }
        for f in failures.iter().take(15) {
            println!("\n{f}");
        }
        return;
    }
    assert!(total > 0, "no fixtures found");
    assert!(
        passed == total,
        "{} of {} fixture cases fail; first ones:\n\n{}",
        total - passed,
        total,
        failures.join("\n\n")
    );
}
