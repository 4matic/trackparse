// Rendering helpers shared by the landing-page demo and the playground.
// trackparse returns no offsets (by design, see SPEC R0.5), so the highlight finds each extracted
// value in the input again. It is a display aid only; the parsed object is the real answer.

import type { Artist, ParsedTrack, Version } from "trackparse";
import { icon } from "./icons";
import type { Lang } from "./lang";

export type Kind =
  | "position"
  | "primary"
  | "featured"
  | "producer"
  | "title"
  | "version"
  | "junk"
  | "year";

export interface Segment {
  text: string;
  kind: Kind | null;
}

export const KIND_LABEL: Record<Kind, string> = {
  position: "position / time",
  primary: "artist",
  featured: "featured",
  producer: "producer",
  title: "title",
  version: "version",
  junk: "junk",
  year: "year",
};

export function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Cut `input` into segments coloured by the field each piece of text ended up in. */
export function highlight(input: string, track: ParsedTrack): Segment[] {
  const owner: (Kind | null)[] = new Array(input.length).fill(null);

  // parse() drops zero-width characters and NFC-normalises (R1), so search a folded copy of the
  // input and map each hit back to the original UTF-16 positions.
  const at: number[] = [];
  let plain = "";
  let lower = "";
  for (let i = 0; i < input.length; i++) {
    const c = input[i];
    if (IGNORABLE.has(c)) continue;
    const l = c.toLowerCase();
    at.push(i);
    plain += c;
    lower += l.length === 1 ? l : c; // keep positions aligned (İ lowercases to two units)
  }

  const mark = (needle: string | null | undefined, kind: Kind) => {
    if (!needle) return;
    const forms = [...new Set([needle, needle.normalize("NFD"), needle.normalize("NFC")])];
    for (const [hay, lowerCase] of [
      [plain, false],
      [lower, true],
    ] as const) {
      for (const form of forms) {
        const key = lowerCase ? [...form].map((c) => (c.toLowerCase().length === c.length ? c.toLowerCase() : c)).join("") : form;
        for (let from = 0; ; ) {
          const hit = hay.indexOf(key, from);
          if (hit < 0) break;
          const start = at[hit];
          const end = at[hit + key.length - 1] + 1;
          let free = true;
          for (let i = start; i < end; i++) {
            if (owner[i] !== null) {
              free = false;
              break;
            }
          }
          if (free) {
            for (let i = start; i < end; i++) owner[i] = kind;
            return;
          }
          from = hit + 1;
        }
      }
    }
  };

  for (const v of track.versions) mark(v.raw, "version");
  for (const j of track.junk) mark(j.raw, "junk");
  mark(track.position?.raw, "position");
  mark(track.timestamp?.raw, "position");
  mark(track.title, "title");
  for (const a of track.artists) mark(a.name, roleKind(a));
  if (track.year !== null) mark(String(track.year), "year");

  const out: Segment[] = [];
  for (let i = 0; i < input.length; i++) {
    const k = owner[i];
    const last = out[out.length - 1];
    if (last && last.kind === k) last.text += input[i];
    else out.push({ text: input[i], kind: k });
  }
  return out;
}

// The characters R1.2 removes.
const IGNORABLE = new Set(["\u00AD", "\u200B", "\u2060", "\uFEFF", "\uE000"]);

export function roleKind(a: Artist): Kind {
  if (a.role === "featured") return "featured";
  if (a.role === "producer") return "producer";
  if (a.role === "remixer") return "version";
  return "primary";
}

export function segmentsHtml(segs: Segment[]): string {
  return segs
    .map((s) =>
      s.kind
        ? `<mark class="tp-seg tp-${s.kind}" title="${KIND_LABEL[s.kind]}">${escapeHtml(s.text)}</mark>`
        : `<span class="tp-gap">${escapeHtml(s.text)}</span>`,
    )
    .join("");
}

export function chip(text: string, kind: string, title?: string, href?: string): string {
  const t = title ? ` title="${escapeHtml(title)}"` : "";
  if (href) return `<a class="tp-chip tp-${kind}" href="${escapeHtml(href)}"${t}>${escapeHtml(text)}</a>`;
  return `<span class="tp-chip tp-${kind}"${t}>${escapeHtml(text)}</span>`;
}

// ---- links into the docs ----------------------------------------------------------------------
// The output names things the docs explain. With the site base (`/trackparse`), the playground
// and the landing demo link each field name to its row in the reference and each value (a role,
// a version type, a junk kind) to the guide section that explains it.

/** A text fragment (`:~:text=start,-suffix`): the browser scrolls to and marks that text. */
export function textFragment(start: string, suffix?: string, prefix?: string): string {
  const enc = (x: string) => encodeURIComponent(x).replace(/-/g, "%2D");
  return `:~:text=${prefix ? `${enc(prefix)}-,` : ""}${enc(start)}${suffix ? `,-${enc(suffix)}` : ""}`;
}

/**
 * Each ParsedTrack field and the text that follows its name in the ParsedTrack table of
 * reference/parse (the start of the type column), so the fragment lands on that row.
 */
const FIELD_ROW: Record<string, string> = {
  artists: "Artist[]",
  title: "string",
  versions: "Version[]",
  junk: "{ raw",
  position: "{ raw",
  timestamp: "{ raw",
  year: "number",
  flags: "{ explicit",
  mode: '"clean"',
  warnings: "Warning[]",
};

/** The row of a ParsedTrack field in the reference. */
export function fieldUrl(base: string, field: string): string {
  const suffix = FIELD_ROW[field];
  return `${base}/reference/parse/#parsedtrack${suffix ? textFragment(field, suffix) : ""}`;
}

/** Guide sections, relative to the site base. */
export const GUIDE = {
  role: {
    primary: "guides/artists/#joiners",
    featured: "guides/artists/#features",
    producer: "guides/artists/#producers",
    remixer: "guides/versions/",
  } as Record<string, string>,
  versionType: "guides/versions/#the-27-types",
  modifiers: "guides/versions/#generic-words-borrow-their-meaning",
  unknownRemixer: "guides/versions/#unknown-remixers",
  unknownArtist: "guides/artists/#unknown-artists",
  junk: "guides/junk-and-flags/#junk",
  flags: "guides/junk-and-flags/#flags",
  year: "guides/junk-and-flags/#year",
  position: "guides/junk-and-flags/#track-numbers",
  timestamp: "guides/junk-and-flags/#cue-times",
  mode: {
    clean: "guides/modes/#how-auto-decides",
    youtube: "guides/modes/#youtube-mode",
    filename: "guides/modes/#filename-mode",
    tracklist: "guides/modes/#tracklist-mode",
  } as Record<string, string>,
};

/** `path` under the site base, or nothing when there is no base (no links wanted). */
const at = (base: string | undefined, path: string | undefined) =>
  base === undefined || path === undefined ? undefined : `${base}/${path}`;

function versionLine(v: Version, base?: string): string {
  const bits: string[] = [chip(v.type, "version", "The version types", at(base, GUIDE.versionType))];
  for (const a of v.artists) bits.push(`by <strong>${escapeHtml(a.name)}</strong>`);
  for (const m of v.modifiers) bits.push(chip(`+${m}`, "version", "Modifiers: type words that didn't win", at(base, GUIDE.modifiers)));
  if (v.descriptor) bits.push(`<em>${escapeHtml(v.descriptor)}</em>`);
  if (v.year !== null) bits.push(chip(String(v.year), "year"));
  if (v.unknownArtist) bits.push(chip("unknown remixer", "junk", "Unknown remixers", at(base, GUIDE.unknownRemixer)));
  return bits.join(" ");
}

/**
 * The field cards shown under the highlighted string. `base` is the site base (`/trackparse`):
 * with it, each field name links to its row in the reference, each value to the guide that
 * explains it, and each warning to its row in the warnings reference.
 */
export function fieldsHtml(t: ParsedTrack, base?: string): string {
  const rows: [string, string][] = [];
  const role = (a: Artist) => chip(a.role, roleKind(a), `The ${a.role} role`, at(base, GUIDE.role[a.role]));
  rows.push([
    "Artists",
    t.artists.length
      ? t.artists
          .map(
            (a) =>
              `<span class="tp-artist">${a.joiner ? `<span class="tp-joiner">${escapeHtml(a.joiner)}</span>` : ""}<strong>${escapeHtml(a.name)}</strong> ${role(a)}</span>`,
          )
          .join("")
      : t.flags.unknownArtist
        ? chip("unknown (ID)", "junk", "Unknown artists", at(base, GUIDE.unknownArtist))
        : `<span class="tp-none">none</span>`,
  ]);
  rows.push([
    "Title",
    t.title
      ? `<strong class="tp-title-text">${escapeHtml(t.title)}</strong>${t.flags.unknownTitle ? ` ${chip("unknown (ID)", "junk")}` : ""}`
      : `<span class="tp-none">empty</span>`,
  ]);
  if (t.versions.length)
    rows.push(["Versions", t.versions.map((v) => `<div>${versionLine(v, base)}</div>`).join("")]);
  if (t.junk.length)
    rows.push([
      "Junk",
      t.junk
        .map((j) => `${chip(j.kind, "junk", "The junk kinds", at(base, GUIDE.junk))} <s>${escapeHtml(j.raw)}</s>`)
        .join("<br>"),
    ]);
  if (t.position) rows.push(["Position", chip(`#${t.position.number}`, "position", "Track numbers", at(base, GUIDE.position))]);
  if (t.timestamp)
    rows.push([
      "Timestamp",
      chip(`${t.timestamp.raw} = ${t.timestamp.seconds} s`, "position", "Cue times", at(base, GUIDE.timestamp)),
    ]);
  if (t.year !== null) rows.push(["Year", chip(String(t.year), "year", "Years", at(base, GUIDE.year))]);
  const flags = (["explicit", "clean"] as const).filter((f) => t.flags[f]);
  if (flags.length) rows.push(["Flags", flags.map((f) => chip(f, "year", "Flags", at(base, GUIDE.flags))).join(" ")]);
  const mode = at(base, GUIDE.mode[t.mode]);
  rows.push(["Mode", mode ? `<a class="tp-mode" href="${mode}" title="The ${t.mode} mode"><code>${t.mode}</code></a>` : `<code>${t.mode}</code>`]);
  if (t.warnings.length)
    rows.push([
      "Warnings",
      t.warnings
        .map((w) => {
          const name = escapeHtml(w);
          // A text fragment scrolls to the warning's row; without support the section opens.
          const href = `${base}/reference/warnings/#track-warnings:~:text=${encodeURIComponent(w)}`;
          return `<span class="tp-warn">${icon.warning}${base === undefined ? name : `<a href="${href}" title="What ${name} means">${name}</a>`}</span>`;
        })
        .join(" "),
    ]);
  // The field name links to its row in the ParsedTrack reference.
  const dt = (k: string) =>
    base === undefined ? k : `<a href="${fieldUrl(base, k.toLowerCase())}" title="${k.toLowerCase()} in the ParsedTrack reference">${k}</a>`;
  return `<dl class="tp-fields">${rows
    .map(([k, v]) => `<div class="tp-field"><dt>${dt(k)}</dt><dd>${v}</dd></div>`)
    .join("")}</dl>`;
}

/** JSON colouring for the output panel, in the site's code colours (custom.css --tp-syn-*). */
export function jsonHtml(value: unknown): string {
  const json = JSON.stringify(value, null, 2);
  return escapeHtml(json).replace(
    /(&quot;(?:[^&]|&(?!quot;))*?&quot;)(\s*:)?|\b(true|false|null)\b|(-?\d+(?:\.\d+)?)|([{}[\],])/g,
    (m, str, colon, lit, num, punct) => {
      if (str) return colon ? `<span class="s-prop">${str}</span><span class="s-punct">${colon}</span>` : `<span class="s-str">${str}</span>`;
      if (lit) return `<span class="s-kw">${lit}</span>`;
      if (num) return `<span class="s-num">${num}</span>`;
      if (punct) return `<span class="s-punct">${punct}</span>`;
      return m;
    },
  );
}

/** The languages of the playground's code panels. */
export type { Lang };

const KEYWORDS: Record<Lang, Set<string>> = {
  js: new Set(["import", "from", "export", "const", "let", "var", "return", "new", "await", "async", "function"]),
  py: new Set(["from", "import", "def", "return", "for", "in", "as", "lambda", "not", "and", "or"]),
  rs: new Set(["use", "let", "mut", "fn", "pub", "struct", "enum", "impl", "for", "in", "if", "else", "match", "return", "as", "ref", "const", "static", "mod", "crate", "self", "where", "while", "loop"]),
  go: new Set(["package", "import", "func", "var", "const", "type", "struct", "map", "for", "range", "if", "else", "return", "go", "defer"]),
};
const CONSTANTS = new Set(["true", "false", "null", "undefined", "True", "False", "None", "Some", "nil"]);

/**
 * A small highlighter for the snippets the playground generates (one import, a call or two).
 * Strings, comments, keywords, constants, numbers, function calls, keyword arguments, punctuation.
 */
export function codeHtml(src: string, lang: Lang): string {
  // Groups: comment, string, number, word, punctuation. A fresh RegExp per call (it is stateful).
  const re =
    lang === "js"
      ? /(\/\/[^\n]*)|("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|`(?:\\.|[^`\\])*`)|\b(\d+(?:\.\d+)?)\b|([A-Za-z_$][\w$]*)|([{}()[\],.;:=])/g
      : lang === "rs"
        ? // Rust: `//` comments, double-quoted strings only (a `'` may start a lifetime).
          /(\/\/[^\n]*)|("(?:\\.|[^"\\\n])*")|\b(\d+(?:\.\d+)?)\b|([A-Za-z_][\w]*)|([{}()[\],.;:=&])/g
        : lang === "go"
          ? // Go: `//` comments, interpreted and raw (backquoted) strings.
            /(\/\/[^\n]*)|("(?:\\.|[^"\\\n])*"|`[^`]*`)|\b(\d+(?:\.\d+)?)\b|([A-Za-z_][\w]*)|([{}()[\],.;:=&])/g
        : /(#[^\n]*)|("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')|\b(\d+(?:\.\d+)?)\b|([A-Za-z_][\w]*)|([{}()[\],.;:=])/g;
  let out = "";
  let last = 0;
  for (let m = re.exec(src); m; m = re.exec(src)) {
    out += escapeHtml(src.slice(last, m.index));
    last = re.lastIndex;
    const [tok, com, str, num, word, punct] = m;
    const rest = src.slice(re.lastIndex);
    let cls = "";
    if (com) cls = "s-com";
    else if (str) cls = "s-str";
    else if (num) cls = "s-num";
    else if (word) {
      if (KEYWORDS[lang].has(word)) cls = "s-kw";
      else if (CONSTANTS.has(word)) cls = "s-const";
      else if (/^\s*\(/.test(rest) || (lang === "rs" && /^!\(/.test(rest))) cls = "s-fn";
      else if (lang === "py" && /^=(?!=)/.test(rest)) cls = "s-param";
      else cls = "s-var";
    } else if (punct) cls = "s-punct";
    out += cls ? `<span class="${cls}">${escapeHtml(tok)}</span>` : escapeHtml(tok);
  }
  return out + escapeHtml(src.slice(last));
}
