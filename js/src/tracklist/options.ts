/** R13.10: tracklist options, sanitised so bad values are ignored (R0.8). */
import type { KeywordOptions, ModeOption, ParseOptions } from "../types.js";
import type { TracklistOptions } from "./types.js";

const MODES = new Set(["auto", "clean", "youtube", "filename", "tracklist"]);
const SPLIT_AND = new Set(["auto", "always", "never"]);

export interface ResolvedOptions {
  parse: ParseOptions;
  audioSeconds: number | null;
  markerWords: unknown;
}

function read(options: unknown, key: string): unknown {
  try {
    return (options as Record<string, unknown>)[key];
  } catch {
    return undefined;
  }
}

export function resolveOptions(options: TracklistOptions | undefined): ResolvedOptions {
  const o: unknown = typeof options === "object" && options !== null ? options : {};
  const mode = read(o, "mode");
  const splitAnd = read(o, "splitAnd");
  const keywords = read(o, "keywords");
  const known = read(o, "knownArtists");
  const audio = read(o, "audioSeconds");
  const parse: ParseOptions = {
    mode: typeof mode === "string" && MODES.has(mode) ? (mode as ModeOption) : "tracklist",
  };
  if (Array.isArray(known)) parse.knownArtists = known as string[];
  if (typeof splitAnd === "string" && SPLIT_AND.has(splitAnd)) {
    parse.splitAnd = splitAnd as "auto" | "always" | "never";
  }
  if (typeof keywords === "object" && keywords !== null && !Array.isArray(keywords)) {
    parse.keywords = keywords as KeywordOptions;
  }
  return {
    parse,
    audioSeconds: typeof audio === "number" && Number.isFinite(audio) && audio >= 0 ? audio : null,
    markerWords: read(o, "markerWords"),
  };
}
