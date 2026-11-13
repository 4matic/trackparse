#!/usr/bin/env node
// Writes go/testdata/artist_cases.json: values from the JS reference for the artist-side unit
// tests (artist_side_test.go), which run before the title side of the Go port exists.
//
//   pnpm --filter trackparse build && node go/scripts/dump_artist_cases.mjs [path/to/js/dist]
//
// Three sections, all taken from inputs of the shared fixtures:
//   artists:  R10 parseArtists of every parseArtists fixture input and of the artist side (the
//             text before the first " - ") of every parse fixture input, with the case options.
//             `input` is already normalized (R1), since normalize.go belongs to the title side.
//   versions: every bracketed version of the parse outputs (each one is R5 on its raw text).
//   tracks:   parse outputs of the handwritten fixtures and the format fixtures, with format()
//             under several format options and allArtists().
import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const specDir = resolve(here, "../../spec");
const dist = resolve(process.argv[2] ?? resolve(here, "../../js/dist"));
const tp = await import(pathToFileURL(join(dist, "index.js")).href);

function files(dir) {
  const out = [];
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, e.name);
    if (e.isDirectory()) out.push(...files(p));
    else if (e.name.endsWith(".json")) out.push(p);
  }
  return out.sort();
}

const formatOptions = [
  {},
  { feat: "artist" },
  { feat: "title" },
  { feat: "omit" },
  { joiners: "canonical" },
  { joiners: "canonical", feat: "artist" },
  { featMarker: "featuring" },
  { versions: "none", producers: false },
  { position: true },
];

const artists = new Map();
const versions = new Map();
const tracks = [];
for (const file of files(join(specDir, "fixtures"))) {
  const doc = JSON.parse(readFileSync(file, "utf8"));
  if (!Array.isArray(doc.cases)) continue;
  const handwritten = file.includes("/handwritten/");
  for (const c of doc.cases) {
    const fn = c.fn ?? doc.defaults?.fn ?? "parse";
    const options = { ...doc.defaults?.options, ...c.options };
    const input = c.input;
    if (typeof input !== "string") continue;
    const addArtists = (text) => {
      const norm = tp.normalize(text);
      const key = JSON.stringify([norm, options]);
      if (!artists.has(key)) {
        const entry = { input: norm, expected: tp.parseArtists(text, options) };
        if (Object.keys(options).length > 0) entry.options = options;
        artists.set(key, entry);
      }
    };
    if (fn === "parseArtists") addArtists(input);
    if (fn !== "parse" && fn !== "format") continue;
    const dash = input.indexOf(" - ");
    if (fn === "parse" && dash > 0) addArtists(input.slice(0, dash));
    const track = tp.parse(input, options);
    for (const v of track.versions) {
      if (v.delimiter === "-") continue;
      const key = JSON.stringify([v.raw, v.delimiter, options.knownArtists ?? null, options.splitAnd ?? null, options.keywords ?? null]);
      if (!versions.has(key)) {
        versions.set(key, Object.keys(options).length > 0 ? { options, version: v } : { version: v });
      }
    }
    if (handwritten || fn === "format") {
      const cases = fn === "format" ? [c.formatOptions ?? {}] : formatOptions;
      tracks.push({
        track,
        formats: cases.map((o) => [o, tp.format(track, o)]),
        allArtists: tp.allArtists(track),
      });
    }
  }
}

const out = {
  specVersion: tp.SPEC_VERSION,
  artists: [...artists.values()],
  versions: [...versions.values()],
  tracks,
};
const target = resolve(here, "../testdata/artist_cases.json");
writeFileSync(target, `${JSON.stringify(out)}\n`);
console.log(
  `${target}: ${out.artists.length} artist lists, ${out.versions.length} versions, ${out.tracks.length} tracks`,
);
