#!/usr/bin/env node
// Validates the C fixture runner's outputs against spec/schema, as the schema checks of the other
// ports' runners do (go/fixtures_test.go checkSchema, js/test/fixtures.test.ts). The C port has no
// JSON Schema validator of its own: the runner writes every output as JSONL and this script
// checks it with the ajv that the JS package already has.
//
//   TRACKPARSE_OUTPUT=c/build/fixture-outputs.jsonl c/build/trackparse-fixtures spec/fixtures/*/*.json
//   node c/scripts/validate_outputs.mjs c/build/fixture-outputs.jsonl
//
// Each line is {"id", "fn", "output"}. parse: parsed-track.schema.json; parseArtists: an array
// of $defs/artist; format and normalize: a string; parseTracklist: tracklist.schema.json;
// applyEdits: tracklist.schema.json#/$defs/editResult. Unknown fns fail. Exits 1 on any invalid
// output (the first 20 are printed), 2 on bad usage. CTest runs it as the `schema` test.

import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
const file = process.argv[2];
if (!file) {
  console.error("usage: node c/scripts/validate_outputs.mjs <outputs.jsonl>");
  process.exit(2);
}

// ajv is a dev dependency of js/ (and spec/generator); resolve it from there.
const require = createRequire(join(root, "js", "package.json"));
const Ajv2020 = require("ajv/dist/2020.js").default ?? require("ajv/dist/2020.js");

const ajv = new Ajv2020({ allErrors: true, strict: false });
const load = (name) => JSON.parse(readFileSync(join(root, "spec", "schema", name), "utf8"));
const trackSchema = load("parsed-track.schema.json");
const tracklistSchema = load("tracklist.schema.json");
ajv.addSchema(trackSchema);
ajv.addSchema(tracklistSchema);
const validators = {
  track: ajv.getSchema(trackSchema.$id),
  artist: ajv.getSchema(`${trackSchema.$id}#/$defs/artist`),
  tracklist: ajv.getSchema(tracklistSchema.$id),
  editResult: ajv.getSchema(`${tracklistSchema.$id}#/$defs/editResult`),
};
for (const [name, v] of Object.entries(validators)) {
  if (!v) {
    console.error(`validate_outputs: cannot resolve the ${name} schema`);
    process.exit(2);
  }
}

/** Everything wrong with one output, or null. */
function problemOf(fn, out) {
  const check = (v, value, label) => (v(value) ? null : `${label}: ${ajv.errorsText(v.errors)}`);
  switch (fn) {
    case "parse":
      return check(validators.track, out, "parse");
    case "parseArtists":
      if (!Array.isArray(out)) return "parseArtists: not an array";
      for (const a of out) {
        const p = check(validators.artist, a, "parseArtists");
        if (p) return p;
      }
      return null;
    case "format":
    case "normalize":
      return typeof out === "string" ? null : `${fn}: not a string`;
    case "parseTracklist":
      return check(validators.tracklist, out, "parseTracklist");
    case "applyEdits":
      return check(validators.editResult, out, "applyEdits");
  }
  return `unknown fn ${JSON.stringify(fn)}`;
}

const lines = readFileSync(file, "utf8").split("\n").filter((l) => l.trim() !== "");
const bad = [];
for (const [k, line] of lines.entries()) {
  let rec;
  try {
    rec = JSON.parse(line);
  } catch (err) {
    bad.push(`line ${k + 1}: not JSON (${err.message})`);
    continue;
  }
  const p = problemOf(rec.fn, rec.output);
  if (p) bad.push(`${rec.id}: ${p}`);
}

if (lines.length === 0) {
  console.error(`validate_outputs: ${file} has no outputs`);
  process.exit(1);
}
if (bad.length > 0) {
  console.log(`${bad.length} of ${lines.length} outputs violate the schema; first ones:`);
  for (const b of bad.slice(0, 20)) console.log(`  ${b}`);
  process.exit(1);
}
console.log(`schema: ${lines.length} outputs valid`);
