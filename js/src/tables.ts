/** Vocabulary tables compiled from spec/data, optionally extended by `options.keywords` (R12). */
import {
  descriptorsData,
  extensionsData,
  featMarkersData,
  genresData,
  joinersData,
  junkData,
  noSplitBeforeData,
  platformSuffixesData,
  stopwordsData,
  unknownTokensData,
  versionKeywordsData,
} from "./data.generated.js";
import type { JunkKind, KeywordOptions, VersionType } from "./types.js";
import { asciiLower, PhraseTable } from "./words.js";

export interface SpacedJoiner {
  raw: string;
  canonical: string;
  isAnd: boolean;
  caseSensitive: boolean;
}

/** Category of a word in the modifier run left of a version head (R5.7.2). */
export interface RunInfo {
  typeCapable?: VersionType;
  descriptor?: string;
  genre?: true;
  head?: VersionType;
}

export interface Tables {
  featMarkers: PhraseTable<true>;
  bracketOnlyFeat: PhraseTable<true>;
  producerMarkers: PhraseTable<true>;
  /** R8.3: the only unbracketed feat markers on the title side (+ keywords.featMarkers). */
  titleFeatMarkers: PhraseTable<true>;
  /** R8.3: the only unbracketed producer markers on the title side. */
  titleProducerMarkers: PhraseTable<true>;
  /** Junk phrases plus genres (kind `genre`) — the R5.1 tokenizer vocabulary. */
  junkOrGenre: PhraseTable<JunkKind>;
  /** Junk phrases only (R8.4). */
  junkPhrases: PhraseTable<JunkKind>;
  junkConnectors: ReadonlySet<string>;
  labelSuffixes: ReadonlySet<string>;
  heads: PhraseTable<VersionType>;
  genericHeadTypes: ReadonlySet<string>;
  /** Everything allowed in the R5.7.2 modifier run, keyed by phrase. */
  runWords: PhraseTable<RunInfo>;
  prefixForms: PhraseTable<VersionType>;
  spacedJoiners: SpacedJoiner[];
  tightJoiners: ReadonlyMap<string, string>;
  featCanonical: string;
  noSplitBefore: ReadonlySet<string>;
  stopwords: ReadonlySet<string>;
  unknownCaseSensitive: ReadonlySet<string>;
  unknownCaseInsensitive: ReadonlySet<string>;
  extensions: ReadonlySet<string>;
  platformSuffixes: readonly string[];
}

/** R0.8: option lists may be anything at runtime; only arrays of strings count. */
export function stringList(value: unknown): string[] {
  return Array.isArray(value) ? value.filter((s): s is string => typeof s === "string") : [];
}

/** R0.8: option maps may be anything at runtime; only plain objects' string values count. */
export function stringEntries(value: unknown): [string, string][] {
  if (typeof value !== "object" || value === null || Array.isArray(value)) return [];
  return Object.entries(value).filter((e): e is [string, string] => typeof e[1] === "string");
}

function lowerKeys(list: unknown): string[] {
  return stringList(list)
    .map((s) => asciiLower(s).trim())
    .filter((s) => s.length > 0);
}

export function buildTables(keywords?: KeywordOptions): Tables {
  const featMarkers = new PhraseTable<true>(
    [...featMarkersData.markers, ...lowerKeys(keywords?.featMarkers)].map((m) => [m, true]),
  );
  const bracketOnlyFeat = new PhraseTable<true>(featMarkersData.bracketOnly.map((m) => [m, true]));
  const producerMarkers = new PhraseTable<true>(featMarkersData.producer.map((m) => [m, true]));
  const titleFeatMarkers = new PhraseTable<true>(
    [...featMarkersData.titleMarkers, ...lowerKeys(keywords?.featMarkers)].map((m) => [m, true]),
  );
  const titleProducerMarkers = new PhraseTable<true>(
    featMarkersData.titleProducer.map((m) => [m, true]),
  );

  const junkEntries: [string, JunkKind][] = [
    ...(Object.entries(junkData.phrases) as [string, JunkKind][]),
    ...(stringEntries(keywords?.junk) as [string, JunkKind][]),
  ];
  const genres = [...genresData.genres, ...lowerKeys(keywords?.genres)];
  const junkPhrases = new PhraseTable<JunkKind>(junkEntries);
  // Junk phrases are added first, so they win over an identical genre key.
  const junkOrGenre = new PhraseTable<JunkKind>([
    ...junkEntries,
    ...genres.map((g): [string, JunkKind] => [g, "genre"]),
  ]);

  const headEntries: [string, VersionType][] = [
    ...(Object.entries(versionKeywordsData.heads) as [string, VersionType][]),
    ...(stringEntries(keywords?.versionHeads) as [string, VersionType][]),
  ];
  const heads = new PhraseTable<VersionType>(headEntries);

  const runInfo = new Map<string, RunInfo>();
  const note = (key: string, patch: RunInfo) => {
    const k = asciiLower(key).trim();
    runInfo.set(k, { ...runInfo.get(k), ...patch });
  };
  for (const [k, t] of Object.entries(versionKeywordsData.typeCapableModifiers)) {
    note(k, { typeCapable: t as VersionType });
  }
  for (const d of [...descriptorsData.descriptors, ...lowerKeys(keywords?.descriptors)]) {
    note(d, { descriptor: asciiLower(d).trim() });
  }
  for (const g of genres) note(g, { genre: true });
  for (const [k, t] of headEntries) note(k, { head: t });

  return {
    featMarkers,
    bracketOnlyFeat,
    producerMarkers,
    titleFeatMarkers,
    titleProducerMarkers,
    junkOrGenre,
    junkPhrases,
    junkConnectors: new Set(junkData.connectors),
    labelSuffixes: new Set(junkData.labelSuffixes),
    heads,
    genericHeadTypes: new Set(versionKeywordsData.genericHeads),
    runWords: new PhraseTable<RunInfo>(runInfo),
    prefixForms: new PhraseTable<VersionType>(
      Object.entries(versionKeywordsData.prefixForms) as [string, VersionType][],
    ),
    spacedJoiners: joinersData.spaced.map((j) => ({
      raw: j.raw,
      canonical: j.canonical,
      isAnd: "kind" in j && j.kind === "and",
      caseSensitive: "caseSensitive" in j && j.caseSensitive === true,
    })),
    tightJoiners: new Map(joinersData.tight.map((j) => [j.raw, j.canonical])),
    featCanonical: joinersData.featCanonical,
    noSplitBefore: new Set(noSplitBeforeData.words),
    stopwords: new Set(stopwordsData.words),
    unknownCaseSensitive: new Set(unknownTokensData.caseSensitive),
    unknownCaseInsensitive: new Set(unknownTokensData.caseInsensitive),
    extensions: new Set(extensionsData.extensions),
    platformSuffixes: [...platformSuffixesData.suffixes].sort((a, b) => b.length - a.length),
  };
}

let defaultTables: Tables | null = null;

export function getDefaultTables(): Tables {
  defaultTables ??= buildTables();
  return defaultTables;
}

export function tablesFor(keywords: KeywordOptions | undefined): Tables {
  return keywords ? buildTables(keywords) : getDefaultTables();
}

/** R7.5 / R8.6: an unknown placeholder name (`ID`, `???`, `Untitled`…). */
export function isUnknownToken(text: string, t: Tables): boolean {
  return t.unknownCaseSensitive.has(text) || t.unknownCaseInsensitive.has(asciiLower(text));
}
