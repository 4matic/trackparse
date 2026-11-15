// fuzz_corpus.mjs: the seed corpus of the libFuzzer targets in c/fuzz/, from every shared fixture
// input (as go/fuzz_test.go seeds its targets).
//
//   node c/scripts/fuzz_corpus.mjs <out-dir>
//
// Writes <out-dir>/<target>/<sha1> files in each target's input encoding:
//   parse, parse_artists   one option-set byte, then the input
//   format, parse_tracklist  8 bytes of seed (little-endian), then the input
//   apply_edits            8 bytes of seed, 4 bytes of text length, the text, the edits JSON
//   wire                   the case as one JSONL line (what compare-ports sends to the dump tool)
// Then, for example:  c/build-fuzz/fuzz_parse -max_total_time=60 <out-dir>/parse
import { createHash } from "node:crypto";
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const fixtures = join(here, "..", "..", "spec", "fixtures");
const out = process.argv[2];
if (!out) {
  console.error("usage: node c/scripts/fuzz_corpus.mjs <out-dir>");
  process.exit(2);
}

const cases = [];
for (const sub of ["handwritten", "generated"]) {
  for (const name of readdirSync(join(fixtures, sub)).sort()) {
    if (!name.endsWith(".json")) continue;
    const data = JSON.parse(readFileSync(join(fixtures, sub, name), "utf8"));
    if (!Array.isArray(data.cases)) continue;
    const defaults = data.defaults ?? {};
    for (const c of data.cases) {
      cases.push({
        fn: c.fn ?? defaults.fn ?? "parse",
        input: c.input ?? "",
        options: { ...(defaults.options ?? {}), ...(c.options ?? {}) },
        formatOptions: { ...(defaults.formatOptions ?? {}), ...(c.formatOptions ?? {}) },
        edits: c.edits ?? [],
      });
    }
  }
}

const written = {};
function write(target, bytes) {
  const dir = join(out, target);
  if (!written[target]) {
    mkdirSync(dir, { recursive: true });
    written[target] = new Set();
  }
  const name = createHash("sha1").update(bytes).digest("hex");
  if (written[target].has(name)) return;
  written[target].add(name);
  writeFileSync(join(dir, name), bytes);
}

const u64 = (n) => {
  const b = Buffer.alloc(8);
  b.writeBigUInt64LE(BigInt(n));
  return b;
};
const u32 = (n) => {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(n);
  return b;
};

cases.forEach((c, i) => {
  const input = Buffer.from(c.input, "utf8");
  if (["parse", "format", "normalize", "parseArtists"].includes(c.fn)) {
    write("parse", Buffer.concat([Buffer.from([i % 256]), input]));
  }
  if (["parseArtists", "parse"].includes(c.fn)) {
    write("parse_artists", Buffer.concat([Buffer.from([i % 256]), input]));
  }
  if (["format", "parse"].includes(c.fn)) write("format", Buffer.concat([u64(i), input]));
  if (["parseTracklist", "applyEdits"].includes(c.fn)) {
    write("parse_tracklist", Buffer.concat([u64(i), input]));
  }
  if (c.fn === "applyEdits" || c.fn === "parseTracklist") {
    const edits = Buffer.from(JSON.stringify(c.fn === "applyEdits" ? c.edits : []), "utf8");
    write("apply_edits", Buffer.concat([u64(i), u32(input.length), input, edits]));
  }
  write("wire", Buffer.from(JSON.stringify(c), "utf8"));
});

for (const [target, set] of Object.entries(written)) {
  console.log(`${target}: ${set.size} seeds`);
}
