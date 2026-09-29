/**
 * Composition model: a case is a Composition of atoms. `render()` produces the input
 * string and the expected (partial) ParsedTrack *from the atoms alone*. `problems()`
 * rejects compositions whose expected output would be ambiguous or would require
 * reasoning the atoms don't encode (see comments on each check; rule ids from SPEC.md).
 */
import {
  type ArtistAtom,
  type Delim,
  type ExtraAtom,
  type FeatMarkerAtom,
  type FeatPlacement,
  type JoinerAtom,
  type JunkAtom,
  type JunkForm,
  type ModeAtom,
  type PrefixAtom,
  type ProducerMarkerAtom,
  type SepAtom,
  type TitleAtom,
  type TransformAtom,
  type VersionAtom,
  isChannel,
} from "./atoms.js";
import { DATA } from "./data.js";
import {
  artistProblem,
  dedupKey,
  endsWithJunkPhrase,
  remixerProblem,
  stripGroups,
  trailingJunkAmbiguous,
  wordKey,
  wordMatches,
  words,
} from "./text.js";

// ---------------------------------------------------------------- types

export interface CreditList {
  names: ArtistAtom[];
  /** joiners[i] sits between names[i] and names[i+1]. */
  joiners: JoinerAtom[];
}

export interface FeatUse {
  placement: FeatPlacement;
  marker: FeatMarkerAtom;
  credit: CreditList;
}

export interface ProducerUse {
  marker: ProducerMarkerAtom;
  credit: CreditList;
  form: "paren" | "square" | "inline";
}

export interface VersionUse {
  atom: VersionAtom;
  delim: Delim;
  /** One credit list per `{R}` slot, in order. */
  credits: CreditList[];
}

export interface JunkUse {
  atom: JunkAtom;
  form: JunkForm;
}

export type SplitAnd = "auto" | "always" | "never";

export interface Composition {
  mode: ModeAtom;
  prefix: PrefixAtom | null;
  /** null = no artist side at all (R6.8). */
  main: CreditList | null;
  feat: FeatUse | null;
  sep: SepAtom;
  title: TitleAtom;
  producer: ProducerUse | null;
  versions: VersionUse[];
  extras: ExtraAtom[];
  /** A year-only dash suffix (`- 2015`). */
  dashYear: number | null;
  junk: JunkUse[];
  splitAnd: SplitAnd;
  transform: TransformAtom | null;
  /** Allow repeated artists (dedup scenario). */
  allowDup?: boolean;
}

export interface ExpArtist {
  name: string;
  role: "primary" | "featured" | "producer" | "remixer";
  joiner: string | null;
  source: "artist" | "title" | "version";
  /** Internal: id of the credit list the name came from (R7.6). Never serialized. */
  list?: number;
}

export interface ExpVersion {
  type: string;
  raw: string;
  artists: string[];
  modifiers: string[];
  descriptor: string | null;
  year: number | null;
  unknownArtist: boolean;
  delimiter: Delim;
}

export interface Rendered {
  input: string;
  options: Record<string, unknown>;
  expected: Record<string, unknown>;
  rules: string[];
  /** Kept for statistics/tests; not serialized (the case id carries the same atoms). */
  tags: string[];
}

export interface AssertOpts {
  /** Always assert position/timestamp (even when null). */
  position?: boolean;
  /** Always assert junk (even when empty). */
  junk?: boolean;
  /** Always assert year/flags. */
  yearFlags?: boolean;
  /** Assert every version field (default: type/raw/artists/delimiter + non-default fields only). */
  fullVersions?: boolean;
}

// ---------------------------------------------------------------- credit lists

export const single = (a: ArtistAtom): CreditList => ({ names: [a], joiners: [] });

export function renderCredit(c: CreditList): string {
  let s = c.names[0]!.text;
  c.joiners.forEach((j, i) => {
    s += j.render + c.names[i + 1]!.text;
  });
  return s;
}

const GUARDED_JOINERS = ["&", "and", "+"];

/**
 * Split a credit list exactly as R7.3 prescribes for *these atoms*. Atoms are
 * individually unsplittable (checked by artistProblem), so the only decision is
 * per-joiner: split or merge.
 */
export function splitCredit(
  c: CreditList,
  ctx: { mainArtistList: boolean; splitAnd: SplitAnd },
  firstJoiner: string | null,
  role: ExpArtist["role"],
  source: ExpArtist["source"],
  list?: number,
): ExpArtist[] {
  const out: ExpArtist[] = [];
  let curName = c.names[0]!.text;
  let curJoiner = firstJoiner;
  let commaSeen = false;
  c.joiners.forEach((j, i) => {
    const next = c.names[i + 1]!;
    let split: boolean;
    switch (j.kind) {
      case "split":
        split = true;
        break;
      case "nosplit":
        split = false;
        break;
      case "and":
        split = ctx.splitAnd === "always" || (ctx.splitAnd === "auto" && commaSeen);
        break;
      case "with":
        split = ctx.mainArtistList;
        break;
    }
    if (split && GUARDED_JOINERS.includes(wordKey(j.raw))) {
      const nextFirst = words(next.text)[0]!;
      if (DATA.noSplitBefore.some((g) => wordMatches(nextFirst, g))) split = false;
    }
    if (split) {
      out.push({ name: curName, role, joiner: curJoiner, source, list });
      curName = next.text;
      curJoiner = j.raw;
      // R7.3: `and` (auto) splits after any tight joiner.
      if (j.raw === "," || j.raw === ";") commaSeen = true;
    } else {
      curName += j.render + next.text;
    }
  });
  out.push({ name: curName, role, joiner: curJoiner, source, list });
  return out;
}

const PRECEDENCE: Record<string, number> = { primary: 2, featured: 1, producer: 0 };

/**
 * R9.2 dedup with R7.6 joiner inheritance: when a removed occurrence was the first remaining
 * name of its list, the next remaining name of that list takes its joiner. Producers are a
 * separate class, deduped only among producers. Role upgrades (a later primary duplicate of a
 * featured name) are never generated.
 */
export function dedup(artists: ExpArtist[]): ExpArtist[] {
  const all = artists.map((a) => ({ a: { ...a }, alive: true }));
  const seen = new Map<string, ExpArtist>();
  for (const e of all) {
    const k = `${e.a.role === "producer" ? "p" : "c"}:${dedupKey(e.a.name)}`;
    const kept = seen.get(k);
    if (!kept) {
      seen.set(k, e.a);
      continue;
    }
    if (PRECEDENCE[e.a.role]! > PRECEDENCE[kept.role]!) throw new Error(`dedup role upgrade not generated: ${e.a.name}`);
    const members = all.filter((x) => x.alive && x.a.list === e.a.list);
    if (members[0] === e && members[1]) members[1].a.joiner = e.a.joiner;
    e.alive = false;
  }
  return all.filter((e) => e.alive).map((e) => e.a);
}

// ---------------------------------------------------------------- rendering

const CLOSE: Record<string, string> = { "(": ")", "[": "]", "{": "}" };

function wrap(delim: "(" | "[" | "{", inner: string): string {
  return `${delim}${inner}${CLOSE[delim]}`;
}

function versionRaws(vu: VersionUse): string[] {
  let slot = 0;
  return vu.atom.parts.map((p) => {
    if (!p.tpl.includes("{R}")) return p.tpl;
    const credit = vu.credits[slot++];
    if (!credit) throw new Error(`version ${vu.atom.id}: missing credit`);
    return p.tpl.replace("{R}", renderCredit(credit));
  });
}

function versionText(vu: VersionUse): string {
  return versionRaws(vu).join(" / ");
}

function isBracketFeat(p: FeatPlacement): boolean {
  return p !== "artist-inline" && p !== "title-inline";
}

function featDelim(p: FeatPlacement): "(" | "[" | "{" {
  if (p.endsWith("square")) return "[";
  if (p.endsWith("curly")) return "{";
  return "(";
}

const DASH_FORMS: JunkForm[] = ["dash"];

interface Pieces {
  artistSide: string;
  titleSide: string;
  /** Everything after the separator up to (excluding) the platform suffix. */
  body: string;
  /** Title side text before any bracketed thing. */
  titleUnbracketedTail: boolean;
}

function pieces(c: Composition): Pieces {
  let artistSide = "";
  if (c.main) {
    artistSide = renderCredit(c.main);
    if (c.feat?.placement === "artist-inline") artistSide += ` ${c.feat.marker.text} ${renderCredit(c.feat.credit)}`;
    else if (c.feat && c.feat.placement.startsWith("artist-"))
      artistSide += ` ${wrap(featDelim(c.feat.placement), `${c.feat.marker.text} ${renderCredit(c.feat.credit)}`)}`;
  }
  let t = c.title.text;
  let bracketAfterTitle = false;
  if (c.feat?.placement === "title-inline") t += ` ${c.feat.marker.text} ${renderCredit(c.feat.credit)}`;
  else if (c.feat && c.feat.placement.startsWith("title-")) {
    t += ` ${wrap(featDelim(c.feat.placement), `${c.feat.marker.text} ${renderCredit(c.feat.credit)}`)}`;
    bracketAfterTitle = true;
  }
  if (c.producer) {
    const inner = `${c.producer.marker.text} ${renderCredit(c.producer.credit)}`;
    if (c.producer.form === "inline") t += ` ${inner}`;
    else {
      t += ` ${wrap(c.producer.form === "square" ? "[" : "(", inner)}`;
      bracketAfterTitle = true;
    }
  }
  for (const vu of c.versions) {
    if (vu.delim === "-") continue;
    t += ` ${wrap(vu.delim, versionText(vu))}`;
    bracketAfterTitle = true;
  }
  for (const e of c.extras) {
    t += ` ${e.text}`;
    bracketAfterTitle = true;
  }
  for (const j of c.junk) {
    if (j.form === "paren" || j.form === "square") {
      t += ` ${wrap(j.form === "paren" ? "(" : "[", j.atom.text)}`;
      bracketAfterTitle = true;
    }
  }
  for (const vu of c.versions) if (vu.delim === "-") t += ` - ${versionText(vu)}`;
  if (c.dashYear !== null) t += ` - ${c.dashYear}`;
  for (const j of c.junk) if (DASH_FORMS.includes(j.form)) t += ` - ${j.atom.text}`;
  for (const j of c.junk) if (j.form === "trailing") t += ` ${j.atom.text}`;
  for (const j of c.junk) if (j.form === "pipe") t += ` | ${j.atom.text}`;
  const prefix = c.prefix?.text ?? "";
  const body = c.main ? `${prefix}${artistSide}${c.sep.text}${t}` : `${prefix}${t}`;
  return { artistSide, titleSide: t, body, titleUnbracketedTail: !bracketAfterTitle };
}

/** Junk entries in order of appearance (matches the render order in pieces()). */
function orderedJunk(c: Composition): { raw: string; kind: string }[] {
  const out: { raw: string; kind: string }[] = [];
  for (const form of ["bracket", "dash", "trailing", "pipe"] as const) {
    for (const j of c.junk) {
      const f = j.form === "paren" || j.form === "square" ? "bracket" : j.form;
      if (f === form) out.push({ raw: j.atom.text, kind: j.atom.kind });
    }
  }
  if (c.mode.platform) out.push({ raw: c.mode.platform.raw, kind: "platform" });
  return out;
}

export function resolveMode(c: Composition): "clean" | "youtube" | "filename" {
  if (c.mode.option !== "auto") return c.mode.option;
  if (c.mode.ext) return "filename";
  if (c.mode.platform) return "youtube";
  if (c.sep.kind === "pipe") return "youtube";
  if (c.junk.some((j) => j.form === "pipe" || j.form === "trailing")) return "youtube";
  // A dash suffix is the string's end: R2.1 fires only if it ends with a junk phrase (`… 2015` does not).
  const lastDash = c.junk.filter((j) => j.form === "dash").pop();
  if (lastDash && endsWithJunkPhrase(lastDash.atom.text)) return "youtube";
  if (c.junk.some((j) => (j.form === "paren" || j.form === "square") && j.atom.kind !== "genre")) return "youtube";
  return "clean";
}

export function renderInput(c: Composition): string {
  let s = pieces(c).body;
  if (c.mode.platform) s += c.mode.platform.text;
  if (c.mode.ext) {
    if (c.mode.underscore) s = s.replace(/ /g, "_");
    s += `.${c.mode.ext}`;
  }
  if (c.transform) s = c.transform.apply(s);
  return s;
}

function expectedVersions(c: Composition): ExpVersion[] {
  const ordered = [...c.versions.filter((v) => v.delim !== "-"), ...c.versions.filter((v) => v.delim === "-")];
  const out: ExpVersion[] = [];
  for (const vu of ordered) {
    const raws = versionRaws(vu);
    let slot = 0;
    vu.atom.parts.forEach((p, i) => {
      let artists: string[] = [];
      let descriptor = p.descriptor;
      if (p.tpl.includes("{R}")) {
        const credit = vu.credits[slot++]!;
        if (p.creditAsDescriptor) {
          // R5.7.2: type version/cover → the credit span is a descriptor, no remixers.
          descriptor = renderCredit(credit) + (p.descriptor ? ` ${p.descriptor}` : "");
        } else {
          artists = splitCredit(credit, { mainArtistList: false, splitAnd: c.splitAnd }, null, "remixer", "version").map(
            (a) => a.name,
          );
        }
      }
      out.push({
        type: p.type,
        raw: raws[i]!,
        artists,
        modifiers: p.modifiers,
        descriptor,
        year: p.year,
        unknownArtist: p.unknownArtist,
        delimiter: vu.delim,
      });
    });
  }
  return out;
}

export function expectedArtists(c: Composition): ExpArtist[] {
  const ctxMain = { mainArtistList: true, splitAnd: c.splitAnd };
  const ctxOther = { mainArtistList: false, splitAnd: c.splitAnd };
  const out: ExpArtist[] = [];
  if (c.main) out.push(...splitCredit(c.main, ctxMain, null, "primary", "artist", 0));
  if (c.feat?.placement.startsWith("artist-"))
    out.push(...splitCredit(c.feat.credit, ctxOther, c.feat.marker.text, "featured", "artist", 1));
  if (c.feat?.placement.startsWith("title-"))
    out.push(...splitCredit(c.feat.credit, ctxOther, c.feat.marker.text, "featured", "title", 1));
  if (c.producer) out.push(...splitCredit(c.producer.credit, ctxOther, c.producer.marker.text, "producer", "title", 2));
  return (c.allowDup ? dedup(out) : out).map(({ list: _list, ...a }) => a);
}

export function render(c: Composition, scenario: string, assert: AssertOpts = {}): Rendered {
  const input = renderInput(c);
  const mode = resolveMode(c);
  const versions = expectedVersions(c);
  let title = c.title.text;
  for (const e of c.extras) if (e.kind === "unknown") title += ` ${e.text}`;
  const fullTitle =
    title +
    versions
      .map((v) => (v.delimiter === "-" ? ` - ${v.raw}` : ` ${wrap(v.delimiter, v.raw)}`))
      .join("");
  const junk = orderedJunk(c);

  // An explicit mode option is echoed verbatim (R2.1); only `auto` resolution is worth asserting.
  const expected: Record<string, unknown> = c.mode.option === "auto" ? { mode } : {};
  const numericFirst = c.main !== null && /^[0-9]/.test(c.main.names[0]!.text);
  if (c.prefix || assert.position || numericFirst) {
    expected.position = c.prefix?.position ?? null;
    expected.timestamp = c.prefix?.timestamp ?? null;
  }
  expected.artists = expectedArtists(c).map(compactArtist);
  expected.title = title;
  // fullTitle differs from title only when there are versions (R9.4).
  if (versions.length > 0) expected.fullTitle = fullTitle;
  expected.versions = assert.fullVersions ? versions : versions.map(compactVersion);
  const yearGroup = c.extras.find((e) => e.kind === "year")?.year ?? null;
  const year = yearGroup ?? c.dashYear;
  if (year !== null || assert.yearFlags || c.extras.length > 0) {
    expected.year = year;
    expected.flags = {
      explicit: c.extras.some((e) => e.kind === "explicit"),
      clean: c.extras.some((e) => e.kind === "clean"),
    };
  }
  if (junk.length > 0 || assert.junk) expected.junk = junk;

  const options: Record<string, unknown> = { mode: c.mode.option };
  if (c.splitAnd !== "auto") options.splitAnd = c.splitAnd;

  return { input, options, expected, rules: rulesOf(c), tags: tagsOf(c, scenario) };
}

/**
 * Compact artist encoding: the first primary credit of the artist side (role primary, joiner
 * null, source artist — the values R7.3/R7.6 give the first name of the main list) is written
 * with the README's string shorthand (name only). Every other credit is a full object, so
 * roles, joiners and sources stay asserted wherever they carry information.
 */
function compactArtist(a: ExpArtist): ExpArtist | string {
  return a.role === "primary" && a.joiner === null && a.source === "artist" ? a.name : a;
}

function compactVersion(v: ExpVersion): Record<string, unknown> {
  const o: Record<string, unknown> = { type: v.type, raw: v.raw, artists: v.artists };
  if (v.modifiers.length) o.modifiers = v.modifiers;
  if (v.descriptor !== null) o.descriptor = v.descriptor;
  if (v.year !== null) o.year = v.year;
  if (v.unknownArtist) o.unknownArtist = true;
  o.delimiter = v.delimiter;
  return o;
}

// ---------------------------------------------------------------- rules / tags

function rulesOf(c: Composition): string[] {
  const r = new Set<string>();
  if (c.versions.length) r.add("R9.4");
  if (c.mode.option === "auto") r.add("R2.1");
  if (c.mode.platform) r.add("R2.2");
  if (c.mode.ext) r.add("R2.3");
  if (c.prefix) c.prefix.rules.forEach((x) => r.add(x));
  if (c.main) c.sep.rules.forEach((x) => r.add(x));
  else r.add("R6.8");
  if (c.sep.kind === "pipe" && c.main) r.add("R2.4");
  const lists: CreditList[] = [];
  if (c.main) lists.push(c.main);
  if (c.feat) {
    lists.push(c.feat.credit);
    const p = c.feat.placement;
    if (p === "artist-inline") r.add("R7.2");
    else if (p === "title-inline") r.add("R8.3");
    else {
      r.add("R5.2");
      r.add(p.startsWith("artist") ? "R7.1" : "R8.1");
    }
    r.add("R9.1");
  }
  if (c.producer) {
    lists.push(c.producer.credit);
    r.add(c.producer.form === "inline" ? "R8.3" : "R5.3");
  }
  for (const v of c.versions) {
    v.atom.rules.forEach((x) => r.add(x));
    lists.push(...v.credits);
    if (v.delim === "-") r.add("R6.2");
    else r.add("R8.1");
  }
  if (lists.some((l) => l.joiners.length > 0)) r.add("R7.3");
  if (c.extras.length) c.extras.forEach((e) => e.rules.forEach((x) => r.add(x)));
  if (c.dashYear !== null) {
    r.add("R6.2");
    r.add("R5.5");
  }
  for (const j of c.junk) {
    if (j.form === "trailing") r.add("R8.4");
    else if (j.form === "pipe") r.add("R2.4");
    else if (j.form === "dash") {
      r.add("R6.2");
      r.add("R5.1");
    } else {
      r.add("R5.1");
      r.add("R8.1");
    }
  }
  if (c.transform) c.transform.rules.forEach((x) => r.add(x));
  if (c.allowDup) {
    r.add("R9.2");
    r.add("R7.6");
  }
  const numericFirst = c.main && /^[0-9]/.test(c.main.names[0]!.text);
  if (numericFirst) r.add("R4.2");
  return [...r].sort(ruleCmp);
}

function ruleCmp(a: string, b: string): number {
  const pa = a.slice(1).split(".").map((x) => parseInt(x, 10));
  const pb = b.slice(1).split(".").map((x) => parseInt(x, 10));
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? -1) - (pb[i] ?? -1);
    if (d !== 0) return d;
  }
  return a < b ? -1 : a > b ? 1 : 0;
}

function tagsOf(c: Composition, scenario: string): string[] {
  const t = new Set<string>([scenario, `mode-${c.mode.option}`]);
  const allNames = [
    ...(c.main?.names ?? []),
    ...(c.feat?.credit.names ?? []),
    ...(c.producer?.credit.names ?? []),
    ...c.versions.flatMap((v) => v.credits.flatMap((cr) => cr.names)),
  ];
  for (const n of allNames) if (n.cat !== "plain") t.add(n.cat);
  if (c.title.cat !== "plain") t.add(`title-${c.title.cat}`);
  if (c.feat) t.add("feat");
  if (c.producer) t.add("producer");
  if (c.versions.length) t.add("version");
  for (const v of c.versions) t.add(`v-${v.atom.parts[0]!.type}`);
  if (c.junk.length || c.mode.platform) t.add("junk");
  if (c.prefix?.position) t.add("position");
  if (c.prefix?.timestamp) t.add("timestamp");
  if (c.sep.kind !== "spaced") t.add(`sep-${c.sep.kind}`);
  if (c.transform && c.transform.id !== "none") t.add(`unicode-${c.transform.id}`);
  if (!c.main) t.add("no-artist");
  if (c.allowDup) t.add("dedup");
  return [...t].sort();
}

// ---------------------------------------------------------------- constraints

const FILE_UNSAFE = /[/:*?"<>|\\]/;
const X_JOINERS = ["x", "x-upper", "times"];

function listProblems(l: CreditList, what: string, remixer: boolean): string | null {
  if (l.joiners.length !== l.names.length - 1) return `${what}: joiner count`;
  for (const n of l.names) {
    const p = remixer ? remixerProblem(n.text) : artistProblem(n.text, true);
    if (p) return `${what}: ${n.id} ${p}`;
  }
  const keys = l.names.map((n) => dedupKey(n.text));
  if (new Set(keys).size !== keys.length) return `${what}: duplicate name in list`;
  // `Malcolm X x Y`: legal per R7.3 (x is case-sensitive) but deliberately avoided.
  if (l.joiners.some((j) => X_JOINERS.includes(j.id)) && l.names.some((n) => /(^| )[xX]( |$)/.test(n.text)))
    return `${what}: X-name next to x joiner`;
  for (let i = 0; i < l.joiners.length; i++) {
    const j = l.joiners[i]!;
    // R7.3: `,` between two digits never splits.
    if (j.id === "comma-tight" && /[0-9]$/.test(l.names[i]!.text) && /^[0-9]/.test(l.names[i + 1]!.text))
      return `${what}: tight comma between digits`;
  }
  return null;
}

/** Every reason this composition is unsafe/ambiguous; empty = OK. First reason is enough for callers. */
export function problem(c: Composition): string | null {
  const mode = resolveMode(c);
  const opt = c.mode.option;
  const pcs = pieces(c);
  const dashItems = c.versions.filter((v) => v.delim === "-").length + (c.dashYear !== null ? 1 : 0) +
    c.junk.filter((j) => j.form === "dash").length;
  const hasChannel = c.junk.some((j) => isChannel(j.atom));
  /** Any spaced dash after the separator (dash suffixes, dashed titles). */
  const titleDashes = pcs.titleSide.includes(" - ");

  // --- credit lists
  if (c.main) {
    const p = listProblems(c.main, "main", false);
    if (p) return p;
  }
  if (c.feat) {
    const p = listProblems(c.feat.credit, "feat", false);
    if (p) return p;
    if (c.feat.marker.bracketOnly && !isBracketFeat(c.feat.placement)) return "bracket-only marker unbracketed";
    // R8.3: undotted `ft`/`feat` (and `feat:`) are plain text on the title side.
    if (c.feat.placement === "title-inline" && !c.feat.marker.titleInline) return "marker is text on the title side";
    // R5.2: `(with You)` guard \u2014 with/w/ followed by a stopword stays Unknown.
    if (c.feat.marker.bracketOnly && DATA.stopwords.includes(wordKey(words(c.feat.credit.names[0]!.text)[0]!)))
      return "with + stopword";
    if (c.feat.placement.startsWith("artist-") && !c.main) return "artist feat without artist side";
  }
  if (c.producer) {
    const p = listProblems(c.producer.credit, "producer", false);
    if (p) return p;
    if (c.producer.form === "inline" && !c.producer.marker.inline) return "producer marker not inline-capable";
  }
  for (const vu of c.versions) {
    const slots = vu.atom.parts.filter((p) => p.tpl.includes("{R}")).length;
    if (vu.credits.length !== slots) return "version credit slots";
    for (const cr of vu.credits) {
      const p = listProblems(cr, "remixer", !vu.atom.parts[0]!.tpl.startsWith("Remix by"));
      if (p) return p;
      if (cr.names.length > 2) return "too many remixers";
      // R5.6 splits groups at top-level ` / ` and `; `.
      if (cr.joiners.some((j) => j.raw === "/" || j.raw === ";")) return "remixer joiner collides with R5.6";
    }
    if (vu.atom.parts.length > 1 && vu.delim === "-") return "multi-version as dash suffix";
  }
  if (!c.allowDup) {
    const names = [
      ...(c.main?.names ?? []),
      ...(c.feat?.credit.names ?? []),
      ...(c.producer?.credit.names ?? []),
    ].map((n) => dedupKey(n.text));
    if (new Set(names).size !== names.length) return "duplicate credit across lists";
  }

  // --- mode prelude
  if (c.mode.platform && opt !== "youtube" && opt !== "auto") return "platform outside youtube";
  if (c.mode.option === "auto") {
    if (c.sep.kind !== "spaced") return "auto with non-spaced separator";
    if (c.transform && c.transform.id !== "none") return "auto with transform";
    if (c.mode.ext && c.mode.platform) return "auto ext+platform";
  }
  if (mode === "filename") {
    if (c.prefix?.timestamp) return "timestamp in filename";
    if (FILE_UNSAFE.test(pcs.body)) return "filename-unsafe character";
    if (c.transform && c.transform.id !== "none") return "transform in filename";
    if (c.mode.platform) return "platform in filename";
  }
  if (mode !== "youtube") {
    if (c.junk.some((j) => j.form === "trailing" || j.form === "pipe")) return "youtube-only junk form";
    if (c.sep.kind === "pipe") return "pipe separator outside youtube";
  }
  if (mode === "clean" && c.sep.kind === "unspaced") return "unspaced dash in clean";

  // --- junk forms
  for (const j of c.junk) if (!j.atom.forms.includes(j.form)) return `junk ${j.atom.id} not allowed as ${j.form}`;
  const trailing = c.junk.filter((j) => j.form === "trailing");
  if (trailing.length > 1) return "several trailing junk";
  if (trailing.length === 1) {
    // R8.3 runs before R8.4: an inline credit list would swallow the junk words.
    if (c.feat?.placement === "title-inline" || c.producer?.form === "inline") return "inline credits + trailing junk";
    // A dash suffix followed by unbracketed junk is not peelable (segment is Unknown).
    if (titleDashes) return "dash suffix + trailing junk";
    const preceding = pcs.titleUnbracketedTail ? words(c.title.text) : [];
    if (trailingJunkAmbiguous(preceding, words(trailing[0]!.atom.text))) return "trailing junk ambiguous";
  }
  if (c.junk.filter((j) => j.form === "pipe").length > 1 && hasChannel) return "several pipe segments incl. channel";

  // --- prefixes (R4)
  const pf = c.prefix;
  if (pf) {
    if (!c.main) return "prefix without artist";
    if (pf.form === "e" && mode !== "filename") return "filename-only position form";
    if (pf.form === "c" && c.sep.kind !== "spaced") return "form (c) needs a spaced dash remainder";
    if (c.sep.kind === "pipe") return "prefix with pipe separator";
    if ((c.sep.kind === "unspaced" || c.sep.kind === "asym") && (pf.timestamp || pf.form !== "a"))
      return "prefix form with unspaced/asym separator";
  }
  if (c.main && !pf?.position) {
    const first = c.main.names[0]!.text;
    const directlySep = c.main.names.length === 1 && !c.feat?.placement.startsWith("artist-");
    if (/^[0-9]{1,3}$/.test(first)) {
      // `311 - Amber` is an artist only when no further spaced dash follows (R4.2c).
      if (mode === "filename") return "digits-only artist in filename (R4.2e)";
      if (c.sep.kind !== "spaced") return "digits-only artist with non-spaced separator";
      if (directlySep && titleDashes) return "digits-only artist + dash suffix (R4.2c)";
    } else if (/^[0-9]{1,3} /.test(first)) {
      if (mode === "filename" && c.sep.kind !== "spaced") return "digit-space artist in filename without spaced dash (R4.2e)";
    }
  }

  // --- separators
  if (c.main) {
    if (c.sep.kind !== "spaced" && titleDashes) return "dash suffix with non-spaced separator";
    if (c.sep.kind !== "spaced" && hasChannel) return "channel pipe with non-spaced separator";
    if (c.sep.kind === "unspaced") {
      if (mode !== "youtube" && mode !== "filename") return "unspaced dash mode";
      if (c.feat && c.feat.placement.startsWith("artist-") && c.feat.placement !== "artist-inline")
        return "artist bracket feat + unspaced";
      if (c.feat?.placement === "title-inline" || c.producer?.form === "inline") return "inline title credits + unspaced";
      if (c.title.text.includes("-")) return "title hyphen + unspaced (R6.7 uses the last candidate)";
      if (!/^\p{L}/u.test(c.title.text)) return "title must start with a letter for R6.7";
      if (pcs.body.includes('"')) return "quote + unspaced";
      if (mode === "youtube" && / by /i.test(stripGroups(pcs.body))) return "' by ' + unspaced (R6.6 first)";
      if (/(^|\s)-(\S|$)|(^|\S)-(\s|$)/.test(pcs.body.replace(c.prefix?.text ?? "", ""))) return "asym dash elsewhere";
    }
    if (c.sep.kind === "asym") {
      const body = pcs.body.slice((c.prefix?.text ?? "").length);
      const asym = body.match(/(?<=\S)-(?= )|(?<= )-(?=\S)/g) ?? [];
      if (asym.length !== 1) return "asym dash count";
      if (body.includes(" - ")) return "spaced dash + asym";
    }
  } else {
    // No artist side: R6.8. Keep it to clean mode and a fixed separator id.
    if (opt !== "clean") return "no-artist outside clean";
    if (c.sep.id !== "hyphen") return "no-artist sep id";
    if (dashItems === 0) return "no-artist without dash suffix";
    for (const v of c.versions) if (v.delim === "-" && !v.atom.peelAlone) return "dash version not peelable alone";
  }

  // --- versions / extras
  if (c.dashYear !== null && c.extras.some((e) => e.kind === "year")) return "two years";
  if (c.extras.filter((e) => e.kind === "year").length > 1) return "two year groups";
  const flagKinds = c.extras.filter((e) => e.kind === "explicit" || e.kind === "clean").map((e) => e.kind);
  if (new Set(flagKinds).size !== flagKinds.length) return "duplicate flags";

  // --- transforms
  if (c.transform && c.transform.id !== "none" && opt === "filename") return "transform in filename";
  return null;
}
