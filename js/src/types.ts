/** Public types. They mirror spec/schema/parsed-track.schema.json and options.schema.json. */

export type Mode = "clean" | "youtube" | "filename";
export type ModeOption = Mode | "auto";

export type ArtistRole = "primary" | "featured" | "remixer" | "producer";
export type ArtistSource = "artist" | "title" | "version";

export interface Artist {
  name: string;
  role: ArtistRole;
  /** Raw joiner / marker text that preceded the name in its credit list; `null` for the first name. */
  joiner: string | null;
  source: ArtistSource;
}

export type VersionType =
  | "remix"
  | "bootleg"
  | "vip"
  | "edit"
  | "flip"
  | "refix"
  | "rework"
  | "mashup"
  | "blend"
  | "dub"
  | "mix"
  | "extended"
  | "radio"
  | "club"
  | "original"
  | "instrumental"
  | "acapella"
  | "live"
  | "acoustic"
  | "remaster"
  | "demo"
  | "reprise"
  | "cover"
  | "version"
  | "spedUp"
  | "slowed"
  | "nightcore";

export type VersionDelimiter = "(" | "[" | "{" | "-";

export interface Version {
  type: VersionType;
  raw: string;
  artists: Artist[];
  modifiers: string[];
  descriptor: string | null;
  year: number | null;
  unknownArtist: boolean;
  delimiter: VersionDelimiter;
}

export type JunkKind =
  | "video"
  | "audio"
  | "lyrics"
  | "quality"
  | "promo"
  | "platform"
  | "label"
  | "genre"
  | "other";

export interface Junk {
  raw: string;
  kind: JunkKind;
}

export type Warning =
  | "noSeparator"
  | "ambiguousSeparator"
  | "unspacedDashSplit"
  | "asymmetricDashSplit"
  | "bySplit"
  | "quotedTitleSplit"
  | "ambiguousMixCredit"
  | "unbalancedBrackets";

export interface Flags {
  explicit: boolean;
  clean: boolean;
  unknownArtist: boolean;
  unknownTitle: boolean;
}

export interface Position {
  raw: string;
  number: number;
}

export interface Timestamp {
  raw: string;
  seconds: number;
}

export interface ParsedTrack {
  input: string;
  mode: Mode;
  position: Position | null;
  timestamp: Timestamp | null;
  artists: Artist[];
  title: string;
  fullTitle: string;
  versions: Version[];
  year: number | null;
  flags: Flags;
  junk: Junk[];
  warnings: Warning[];
}

export interface KeywordOptions {
  /** Extra version heads: key → version type. */
  versionHeads?: Record<string, VersionType>;
  descriptors?: string[];
  genres?: string[];
  /** Extra junk phrases: phrase → kind. */
  junk?: Record<string, JunkKind>;
  featMarkers?: string[];
}

export interface ParseOptions {
  mode?: ModeOption;
  /** youtube mode: channel name used as the artist when no separator is found (R6.8). */
  uploader?: string;
  /** Names that must never be split (R7.3a). */
  knownArtists?: string[];
  splitAnd?: "auto" | "always" | "never";
  keywords?: KeywordOptions;
}

export interface FormatOptions {
  feat?: "source" | "artist" | "title" | "omit";
  featMarker?: string;
  joiners?: "original" | "canonical";
  versions?: "all" | "none";
  producers?: boolean;
  position?: boolean;
}
