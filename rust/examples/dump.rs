//! The Rust side of the cross-port diff: JSONL in, JSON out.
//!
//! Each stdin line is an object `{ "fn", "input", "options", "formatOptions", "edits" }` (as in the
//! shared fixtures); each stdout line is the JSON result of `trackparse::wire::call`. A line that
//! is not a JSON object gives `null`.
//!
//! ```sh
//! cargo run --release --example dump < cases.jsonl > results.jsonl
//! ```

use std::io::{BufRead, BufWriter, Write};

use trackparse::json::Json;
use trackparse::wire;

fn run(line: &str) -> Json {
    let Some(case) = Json::parse(line) else { return Json::Null };
    let func = case.get("fn").and_then(Json::as_str).unwrap_or("parse");
    let input = case.get("input").and_then(Json::as_str).unwrap_or("");
    let empty = Json::Obj(Vec::new());
    let options = case.get("options").unwrap_or(&empty);
    let format_options = case.get("formatOptions").unwrap_or(&empty);
    let no_edits = Json::Arr(Vec::new());
    let edits = case.get("edits").unwrap_or(&no_edits);
    wire::call(func, input, options, format_options, edits)
}

fn main() -> std::io::Result<()> {
    let stdin = std::io::stdin();
    let mut out = BufWriter::new(std::io::stdout().lock());
    for line in stdin.lock().lines() {
        let line = line?;
        writeln!(out, "{}", run(&line).write())?;
    }
    out.flush()
}
