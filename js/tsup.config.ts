import { defineConfig } from "tsup";

export default defineConfig({
  // Two public entries: "." and "./tracklist". ESM code-splits the shared core into a chunk that
  // both entries import; CJS bundles each entry on its own (scripts/size-check.mjs measures both).
  entry: { index: "src/index.ts", tracklist: "src/tracklist/index.ts" },
  format: ["esm", "cjs"],
  dts: true,
  target: "es2020",
  clean: true,
  sourcemap: false,
  treeshake: true,
});
