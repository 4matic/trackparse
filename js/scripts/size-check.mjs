#!/usr/bin/env node
/**
 * Size budget for the `trackparse/tracklist` entry: its own code may add at most 8 KiB gzip on
 * top of the core.
 *
 * - ESM: tsup code-splits the shared core into a chunk that dist/index.js and dist/tracklist.js
 *   both import, so dist/tracklist.js is the tracklist code alone: gzip(tracklist.js) <= 8 KiB.
 * - CJS: each entry is bundled on its own, so dist/tracklist.cjs contains the core too:
 *   gzip(tracklist.cjs) - gzip(index.cjs) <= 8 KiB.
 *
 * Usage: node scripts/size-check.mjs   (after `pnpm build`). Zero dependencies.
 */
import { readFileSync } from "node:fs";
import { gzipSync } from "node:zlib";

const BUDGET = 8 * 1024;
const gz = (file) => gzipSync(readFileSync(new URL(`../dist/${file}`, import.meta.url))).length;

const checks = [
  { label: "esm  gzip(tracklist.js)", bytes: gz("tracklist.js") },
  {
    label: "cjs  gzip(tracklist.cjs) - gzip(index.cjs)",
    bytes: gz("tracklist.cjs") - gz("index.cjs"),
  },
];

let failed = false;
for (const { label, bytes } of checks) {
  const ok = bytes <= BUDGET;
  if (!ok) failed = true;
  console.log(`${ok ? "ok  " : "FAIL"} ${label}: ${bytes} B (budget ${BUDGET} B)`);
}
console.log(
  `reference: gzip(index.js) ${gz("index.js")} B (esm, core in a chunk), gzip(index.cjs) ${gz("index.cjs")} B`,
);
process.exit(failed ? 1 : 0);
