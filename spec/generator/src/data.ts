/**
 * Loads the shared vocabulary from spec/data. The generator uses it only for
 * *safety checks on atoms* (e.g. "does this title end in a version head?") so that
 * compositions stay unambiguous. Expected values are never derived by parsing.
 */
import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const GENERATOR_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const SPEC_ROOT = resolve(GENERATOR_ROOT, "..");
export const FIXTURES_ROOT = join(SPEC_ROOT, "fixtures");
export const GENERATED_DIR = join(FIXTURES_ROOT, "generated");

function load<T>(name: string): T {
  return JSON.parse(readFileSync(join(SPEC_ROOT, "data", name), "utf8")) as T;
}

interface JoinerEntry {
  raw: string;
  canonical: string;
  kind?: string;
  caseSensitive?: boolean;
}

const versionKeywords = load<{
  heads: Record<string, string>;
  genericHeads: string[];
  typeCapableModifiers: Record<string, string>;
  prefixForms: Record<string, string>;
}>("version-keywords.json");
const featMarkers = load<{ markers: string[]; bracketOnly: string[]; producer: string[] }>(
  "feat-markers.json",
);
const joiners = load<{ spaced: JoinerEntry[]; tight: JoinerEntry[] }>("joiners.json");
const junk = load<{ phrases: Record<string, string>; connectors: string[]; labelSuffixes: string[] }>(
  "junk.json",
);

export const DATA = {
  heads: versionKeywords.heads,
  genericHeads: versionKeywords.genericHeads,
  typeCapable: versionKeywords.typeCapableModifiers,
  prefixForms: versionKeywords.prefixForms,
  descriptors: load<{ descriptors: string[] }>("descriptors.json").descriptors,
  genres: load<{ genres: string[] }>("genres.json").genres,
  featMarkers: featMarkers.markers,
  bracketOnlyMarkers: featMarkers.bracketOnly,
  producerMarkers: featMarkers.producer,
  spacedJoiners: joiners.spaced,
  tightJoiners: joiners.tight,
  junkPhrases: junk.phrases,
  junkConnectors: junk.connectors,
  labelSuffixes: junk.labelSuffixes,
  noSplitBefore: load<{ words: string[] }>("no-split-before.json").words,
  stopwords: load<{ words: string[] }>("stopwords.json").words,
  unknownCaseSensitive: load<{ caseSensitive: string[] }>("unknown-tokens.json").caseSensitive,
  unknownCaseInsensitive: load<{ caseInsensitive: string[] }>("unknown-tokens.json").caseInsensitive,
  extensions: load<{ extensions: string[] }>("extensions.json").extensions,
  platformSuffixes: load<{ suffixes: string[] }>("platform-suffixes.json").suffixes,
};
