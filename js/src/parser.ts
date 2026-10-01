/** Pipeline: R1 → R2 → R4 → R3/R6 → R7 → R8 → R9, plus R10 parseArtists. */
import { parseArtistSide } from "./artists.js";
import { byPos, dedupArtists, fullTitleOf, toVersion } from "./assemble.js";
import { type Ctx, makeCtx, warn } from "./context.js";
import { type Piece, prelude } from "./mode.js";
import { normalize } from "./normalize.js";
import { stripPrefixes } from "./prefix.js";
import { type Skel, scan, trimSkel } from "./scanner.js";
import { findSeparator } from "./separator.js";
import { stringEntries, stringList, type Tables, tablesFor } from "./tables.js";
import { addPeeled, emptyExtracted, parseTitleSide } from "./title.js";
import type { Artist, Mode, ParsedTrack, ParseOptions } from "./types.js";

export interface Parser {
  parse(input: string, options?: ParseOptions): ParsedTrack;
  parseArtists(input: string, options?: ParseOptions): Artist[];
}

/** Offset used for uploader text so it sorts before everything from the input. */
const UPLOADER_BASE = -1_000_000_000;

function emptyTrack(input: string, mode: Mode): ParsedTrack {
  return {
    input,
    mode,
    position: null,
    timestamp: null,
    artists: [],
    title: "",
    fullTitle: "",
    versions: [],
    year: null,
    flags: { explicit: false, clean: false, unknownArtist: false, unknownTitle: false },
    junk: [],
    warnings: [],
  };
}

function skelOf(piece: Piece, ctx: Ctx): Skel {
  const r = scan(piece.text, piece.base);
  if (r.unbalanced) warn(ctx, "unbalancedBrackets");
  return trimSkel(r.skel);
}

/** R6.8: uploader minus a trailing ` - Topic` and `VEVO`. */
function uploaderSkel(uploader: string | undefined): Skel | null {
  if (typeof uploader !== "string") return null;
  let u = normalize(uploader);
  if (u.endsWith(" - Topic")) u = u.slice(0, -" - Topic".length);
  if (u.endsWith("VEVO")) u = u.slice(0, -"VEVO".length).trimEnd();
  if (u.length === 0) return null;
  return scan(u, UPLOADER_BASE).skel;
}

function run(input: string, options: ParseOptions, t: Tables): ParsedTrack {
  const ctx = makeCtx(t, options);
  const norm = normalize(input);
  const requested = options.mode ?? "auto";
  const pre = prelude(norm, requested, ctx);
  const track = emptyTrack(typeof input === "string" ? input : "", pre.mode);
  if (norm.length === 0) return track; // R9.5

  // R4
  const prefix = stripPrefixes(pre.working.text, pre.working.base, pre.mode, ctx);
  track.timestamp = prefix.timestamp;
  track.position = prefix.position;
  const working = skelOf({ text: prefix.rest, base: prefix.base }, ctx);

  const out = emptyExtracted();
  let artistSkel: Skel | null;
  let titleSkel: Skel;
  let relaxed: boolean;
  if (pre.pipeTitle) {
    // R2.4 step 3: `artist | title`, R6 skipped.
    artistSkel = working.text.length > 0 ? working : null;
    titleSkel = skelOf(pre.pipeTitle, ctx);
    relaxed = artistSkel !== null;
  } else {
    const sep = findSeparator(
      { skel: working, mode: pre.mode, uploader: uploaderSkel(options.uploader) },
      ctx,
    );
    addPeeled(out, sep.peeled);
    artistSkel = sep.artist;
    titleSkel = sep.title;
    relaxed = sep.split;
  }

  const artistSide = artistSkel ? parseArtistSide(artistSkel, ctx) : null;
  const titleSide = parseTitleSide(
    { skel: titleSkel, relaxed, youtube: pre.mode === "youtube" },
    out,
    ctx,
  );

  // R9
  const versions = byPos(out.versions);
  for (const v of versions) if (v.ambiguousMix) warn(ctx, "ambiguousMixCredit");
  track.artists = dedupArtists([...(artistSide?.credits ?? []), ...byPos(out.credits)]);
  track.title = titleSide.title;
  track.versions = versions.map(toVersion);
  track.fullTitle = fullTitleOf(track.title, track.versions);
  // R9.6: the first year in text order.
  const years = byPos([...(artistSide?.years ?? []), ...out.years]);
  track.year = years[0]?.year ?? null;
  track.flags = {
    explicit: out.flags.explicit || (artistSide?.explicit ?? false),
    clean: out.flags.clean || (artistSide?.clean ?? false),
    unknownArtist: (artistSide?.unknownArtist ?? false) || out.flags.unknownArtist,
    unknownTitle: titleSide.unknownTitle,
  };
  track.junk = byPos([...pre.junk, ...(artistSide?.junk ?? []), ...out.junk]).map((j) => ({
    raw: j.raw,
    kind: j.kind,
  }));
  track.warnings = ctx.warnings;
  return track;
}

function runArtists(input: string, options: ParseOptions, t: Tables): Artist[] {
  const ctx = makeCtx(t, options);
  const norm = normalize(input);
  if (norm.length === 0) return [];
  const side = parseArtistSide(scan(norm, 0).skel, ctx);
  return dedupArtists(side.credits).map((a) => ({ ...a, source: "artist" }));
}

function safeOptions(options: ParseOptions | undefined): ParseOptions {
  if (options === null || typeof options !== "object") return {};
  // An explicitly `undefined` per-call key must not override a createParser() base option.
  return Object.fromEntries(
    Object.entries(options).filter(([, v]) => v !== undefined),
  ) as ParseOptions;
}

/** A parser with `options.keywords` compiled once. Per-call options are merged over `base`. */
export function createParser(base: ParseOptions = {}): Parser {
  const baseOptions = safeOptions(base);
  const baseTables = tablesFor(baseOptions.keywords);
  const resolve = (options: ParseOptions | undefined) => {
    const merged = { ...baseOptions, ...safeOptions(options) };
    const tables = options?.keywords ? tablesFor(mergeKeywords(baseOptions, options)) : baseTables;
    return { merged, tables };
  };
  return {
    parse(input, options) {
      const { merged, tables } = resolve(options);
      return run(input, merged, tables);
    },
    parseArtists(input, options) {
      const { merged, tables } = resolve(options);
      return runArtists(input, merged, tables);
    },
  };
}

function mergeKeywords(a: ParseOptions, b: ParseOptions): ParseOptions["keywords"] {
  const x: Record<string, unknown> = isRecord(a.keywords) ? a.keywords : {};
  const y: Record<string, unknown> = isRecord(b.keywords) ? b.keywords : {};
  const map = (k: string) =>
    Object.fromEntries([...stringEntries(x[k]), ...stringEntries(y[k])]) as Record<string, never>;
  const list = (k: string) => [...stringList(x[k]), ...stringList(y[k])];
  return {
    versionHeads: map("versionHeads"),
    descriptors: list("descriptors"),
    genres: list("genres"),
    junk: map("junk"),
    featMarkers: list("featMarkers"),
  };
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}

const defaultParser = createParser();

export function parse(input: string, options?: ParseOptions): ParsedTrack {
  return defaultParser.parse(input, options);
}

export function parseArtists(input: string, options?: ParseOptions): Artist[] {
  return defaultParser.parseArtists(input, options);
}
