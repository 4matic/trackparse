/** R2: mode resolution, platform suffixes, filename handling and youtube pipes. */
import { junkKindOf } from "./classify.js";
import { type Ctx, warn } from "./context.js";
import {
  groupsOf,
  hasSpacedDash,
  renderSkel,
  type Segment,
  type Skel,
  scan,
  sliceSkel,
} from "./scanner.js";
import type { JunkKind, Mode, ModeOption } from "./types.js";
import { asciiLower, collapseSpaces, splitWords, wordSpans } from "./words.js";

export interface PositionedJunk {
  raw: string;
  kind: JunkKind;
  pos: number;
}

/** A contiguous piece of the normalized string and its absolute offset. */
export interface Piece {
  text: string;
  base: number;
}

export interface Prelude {
  mode: Mode;
  /** The working string (R2.4 step 2/4), or the artist piece in the `artist | title` case. */
  working: Piece;
  /** R2.4 step 3: the title piece when two dash-less pipe segments remain. */
  pipeTitle: Piece | null;
  junk: PositionedJunk[];
}

/** R2.2: strip trailing platform suffixes (longest first, repeatedly). */
function stripPlatformSuffixes(s: string, ctx: Ctx, junk: PositionedJunk[]): string {
  let str = s;
  let stripped = true;
  while (stripped) {
    stripped = false;
    const lower = asciiLower(str);
    for (const suffix of ctx.t.platformSuffixes) {
      if (!lower.endsWith(asciiLower(suffix))) continue;
      const start = str.length - suffix.length;
      junk.push({ raw: str.slice(start + 3), kind: "platform", pos: start + 3 });
      str = str.slice(0, start);
      stripped = true;
      break;
    }
  }
  return str;
}

function extensionOf(s: string, ctx: Ctx): string | null {
  const dot = s.lastIndexOf(".");
  if (dot < 0) return null;
  const ext = asciiLower(s.slice(dot + 1));
  return ctx.t.extensions.has(ext) ? ext : null;
}

/** Top-level ` | ` / ` || ` separators on a skeleton → trimmed segment ranges. */
export function splitPipes(skel: Skel): Segment[] {
  const t = skel.text;
  const cuts: [number, number][] = [];
  for (let i = 1; i < t.length; i++) {
    if (t[i - 1] !== " " || t[i] !== "|") continue;
    const bars = t[i + 1] === "|" ? 2 : 1;
    if (t[i + bars] === " " && t[i + bars] !== undefined) {
      cuts.push([i - 1, i + bars + 1]);
      i += bars;
    }
  }
  const segments: Segment[] = [];
  let start = 0;
  for (const [a, b] of [...cuts, [t.length, t.length] as [number, number]]) {
    let s = start;
    let e = a;
    while (s < e && t[s] === " ") s++;
    while (e > s && t[e - 1] === " ") e--;
    segments.push({ skel: sliceSkel(skel, s, e), start: s, end: e });
    start = b;
  }
  return segments;
}

/** R8.4 test: the skeleton ends with an unbracketed junk phrase (genres excluded). */
export function trailingJunkPhrase(
  text: string,
  ctx: Ctx,
): { start: number; kind: JunkKind } | null {
  const spans = wordSpans(text);
  const m = ctx.t.junkPhrases.matchEnding(
    spans.map((s) => s.text),
    spans.length,
  );
  if (!m) return null;
  const first = spans[spans.length - m.length];
  return first ? { start: first.start, kind: m.entry.value } : null;
}

function isJunkText(text: string, ctx: Ctx): JunkKind | null {
  const words = splitWords(text);
  return words.length > 0 ? junkKindOf(words, ctx) : null;
}

/** R2.1 youtube signals (besides platform suffixes). */
function looksLikeYoutube(skel: Skel, ctx: Ctx): boolean {
  if (splitPipes(skel).length > 1) return true;
  for (const { group } of groupsOf(skel)) {
    const kind = isJunkText(group.inner, ctx);
    if (kind && kind !== "genre") return true;
  }
  return trailingJunkPhrase(skel.text, ctx) !== null;
}

export function prelude(normalized: string, requested: ModeOption, ctx: Ctx): Prelude {
  const junk: PositionedJunk[] = [];
  let str = stripPlatformSuffixes(normalized, ctx, junk);
  const hadPlatform = junk.length > 0;

  let mode: Mode;
  if (requested === "clean" || requested === "youtube" || requested === "filename") {
    mode = requested;
  } else if (extensionOf(str, ctx)) {
    mode = "filename";
  } else {
    mode = "clean";
  }

  if (mode === "filename") {
    // R2.3: strip the extension, trim, then `_` → space when there is no space.
    if (extensionOf(str, ctx)) str = str.slice(0, str.lastIndexOf("."));
    str = str.trim();
    if (!str.includes(" ") && str.includes("_")) str = str.split("_").join(" ").trim();
  }

  const { skel, unbalanced } = scan(str, 0);
  if (unbalanced) warn(ctx, "unbalancedBrackets");
  if (requested === "auto" || requested === undefined) {
    if (mode === "clean" && (hadPlatform || looksLikeYoutube(skel, ctx))) mode = "youtube";
  }

  const whole: Piece = { text: str, base: 0 };
  if (mode !== "youtube") return { mode, working: whole, pipeTitle: null, junk };
  return { mode, ...splitYoutubePipes(skel, ctx, junk), junk };
}

function pieceOf(seg: Segment): Piece {
  return { text: renderSkel(seg.skel), base: seg.skel.pos[0] ?? 0 };
}

/** R2.4 */
function splitYoutubePipes(
  skel: Skel,
  ctx: Ctx,
  junk: PositionedJunk[],
): { working: Piece; pipeTitle: Piece | null } {
  const segments = splitPipes(skel);
  if (segments.length <= 1)
    return { working: { text: renderSkel(skel), base: 0 }, pipeTitle: null };

  const remaining: Segment[] = [];
  for (const seg of segments) {
    const text = collapseSpaces(renderSkel(seg.skel));
    // R2.4 step 1: a segment with a spaced dash is an artist/title candidate, never junk.
    const kind = hasSpacedDash(seg.skel.text) ? null : isJunkText(text, ctx);
    if (kind) junk.push({ raw: text, kind, pos: seg.skel.pos[0] ?? 0 });
    else if (text.length > 0) remaining.push(seg);
  }
  const pushOther = (keep: Segment | null, title: Segment | null = null) => {
    for (const seg of remaining) {
      if (seg === keep || seg === title) continue;
      junk.push({
        raw: collapseSpaces(renderSkel(seg.skel)),
        kind: "other",
        pos: seg.skel.pos[0] ?? 0,
      });
    }
  };

  const withDash = remaining.find((seg) => hasSpacedDash(seg.skel.text));
  if (withDash) {
    pushOther(withDash);
    return { working: pieceOf(withDash), pipeTitle: null };
  }
  const [first, second] = remaining;
  if (remaining.length === 2 && first && second) {
    warn(ctx, "ambiguousSeparator");
    return { working: pieceOf(first), pipeTitle: pieceOf(second) };
  }
  if (first) {
    pushOther(first);
    return { working: pieceOf(first), pipeTitle: null };
  }
  // Every segment was junk: nothing left to parse.
  return { working: { text: "", base: 0 }, pipeTitle: null };
}
