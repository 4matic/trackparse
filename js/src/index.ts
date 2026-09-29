/**
 * trackparse — spec-driven parser for music track strings.
 * Behaviour is defined by spec/SPEC.md; rule IDs (R6.2…) in comments refer to it.
 */
export { SPEC_VERSION } from "./data.generated.js";
export { allArtists, format } from "./format.js";
export { normalize } from "./normalize.js";
export { createParser, type Parser, parse, parseArtists } from "./parser.js";
export type {
  Artist,
  ArtistRole,
  ArtistSource,
  Flags,
  FormatOptions,
  Junk,
  JunkKind,
  KeywordOptions,
  Mode,
  ModeOption,
  ParsedTrack,
  ParseOptions,
  Position,
  Timestamp,
  Version,
  VersionDelimiter,
  VersionType,
  Warning,
} from "./types.js";
