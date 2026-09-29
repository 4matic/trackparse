/**
 * R3: top-level group scanner and skeleton.
 *
 * A `Skel` is a skeleton string plus, for every UTF-16 unit, its absolute offset in the
 * normalized input. Groups are looked up by the offset of their placeholder, so any slice of a
 * skeleton still knows which groups it contains. Offsets also give every extracted item (junk,
 * versions, credits) its position for the "order of appearance" rules.
 */
import { collapseSpaces, isWhitespace } from "./words.js";

/** R3.4 placeholder code point. */
export const PH = "";

export type Opener = "(" | "[" | "{";

export interface Group {
  open: Opener;
  /** R3.5: inner text, trimmed and whitespace-collapsed. */
  inner: string;
  /** The group exactly as written, brackets included. */
  raw: string;
  closed: boolean;
  /** Absolute offset of the opener. */
  pos: number;
  /** Absolute offset just past the group. */
  end: number;
}

export interface Skel {
  text: string;
  pos: readonly number[];
  groups: ReadonlyMap<number, Group>;
}

const CLOSER: Record<Opener, string> = { "(": ")", "[": "]", "{": "}" };

function isOpener(ch: string | undefined): ch is Opener {
  return ch === "(" || ch === "[" || ch === "{";
}

export interface ScanResult {
  skel: Skel;
  unbalanced: boolean;
}

/** R3.1–R3.5: scan `str` (whose first unit sits at absolute offset `base`). */
export function scan(str: string, base = 0): ScanResult {
  let text = "";
  const pos: number[] = [];
  const groups = new Map<number, Group>();
  const stack: string[] = [];
  let groupStart = -1;
  for (let i = 0; i < str.length; i++) {
    const ch = str[i] as string;
    if (stack.length === 0) {
      if (isOpener(ch)) {
        stack.push(CLOSER[ch]);
        groupStart = i;
      } else {
        text += ch;
        pos.push(base + i);
      }
      continue;
    }
    if (isOpener(ch)) {
      stack.push(CLOSER[ch]);
    } else if (ch === stack[stack.length - 1]) {
      stack.pop();
      if (stack.length === 0) {
        addGroup(str, groupStart, i + 1, true, base, groups);
        text += PH;
        pos.push(base + groupStart);
      }
    }
    // R3.2: a non-matching closer is literal text inside the group.
  }
  const unbalanced = stack.length > 0;
  if (unbalanced) {
    // R3.3: the group runs to the end of the string.
    addGroup(str, groupStart, str.length, false, base, groups);
    text += PH;
    pos.push(base + groupStart);
  }
  return { skel: { text, pos, groups }, unbalanced };
}

function addGroup(
  str: string,
  start: number,
  end: number,
  closed: boolean,
  base: number,
  groups: Map<number, Group>,
): void {
  const open = str[start] as Opener;
  const inner = str.slice(start + 1, closed ? end - 1 : end);
  groups.set(base + start, {
    open,
    inner: collapseSpaces(inner),
    raw: str.slice(start, end),
    closed,
    pos: base + start,
    end: base + end,
  });
}

// ---------------------------------------------------------------------------
// Skeleton operations

export function emptySkel(groups: ReadonlyMap<number, Group>): Skel {
  return { text: "", pos: [], groups };
}

export function sliceSkel(s: Skel, start: number, end = s.text.length): Skel {
  const a = Math.max(0, Math.min(start, s.text.length));
  const b = Math.max(a, Math.min(end, s.text.length));
  return { text: s.text.slice(a, b), pos: s.pos.slice(a, b), groups: s.groups };
}

export function trimSkel(s: Skel): Skel {
  let a = 0;
  let b = s.text.length;
  while (a < b && isWhitespace(s.text[a])) a++;
  while (b > a && isWhitespace(s.text[b - 1])) b--;
  return sliceSkel(s, a, b);
}

export function concatSkel(a: Skel, b: Skel): Skel {
  return { text: a.text + b.text, pos: [...a.pos, ...b.pos], groups: a.groups };
}

/** Remove the units in [start, end). */
export function cutSkel(s: Skel, start: number, end: number): Skel {
  return concatSkel(sliceSkel(s, 0, start), sliceSkel(s, end));
}

/** The group behind the placeholder at `index`, if it is one. */
export function groupAt(s: Skel, index: number): Group | undefined {
  if (s.text[index] !== PH) return undefined;
  const p = s.pos[index];
  return p === undefined ? undefined : s.groups.get(p);
}

/** Groups in the skeleton, left to right, with their unit index. */
export function groupsOf(s: Skel): { index: number; group: Group }[] {
  const out: { index: number; group: Group }[] = [];
  for (let i = 0; i < s.text.length; i++) {
    const g = groupAt(s, i);
    if (g) out.push({ index: i, group: g });
  }
  return out;
}

export function hasGroup(s: Skel): boolean {
  return s.text.includes(PH);
}

/** Substitute groups back (R3.4 reassembly). Whitespace is not touched. */
export function renderSkel(s: Skel): string {
  if (!s.text.includes(PH)) return s.text;
  let out = "";
  for (let i = 0; i < s.text.length; i++) {
    const g = groupAt(s, i);
    out += g ? g.raw : s.text[i];
  }
  return out;
}

/** Absolute offset of the first unit (or `fallback` for an empty skeleton). */
export function startPos(s: Skel, fallback = 0): number {
  return s.pos[0] ?? fallback;
}

/** Remove the placeholders of the given groups. */
export function removeGroups(s: Skel, remove: ReadonlySet<number>): Skel {
  if (remove.size === 0) return s;
  let text = "";
  const pos: number[] = [];
  for (let i = 0; i < s.text.length; i++) {
    const p = s.pos[i] as number;
    if (s.text[i] === PH && remove.has(p)) continue;
    text += s.text[i];
    pos.push(p);
  }
  return { text, pos, groups: s.groups };
}

/** R6.1: `-` with a space immediately on both sides. */
export function isSpacedDashAt(text: string, i: number): boolean {
  return text[i] === "-" && text[i - 1] === " " && text[i + 1] === " ";
}

export function hasSpacedDash(text: string): boolean {
  for (let i = 1; i < text.length - 1; i++) if (isSpacedDashAt(text, i)) return true;
  return false;
}

export interface Segment {
  skel: Skel;
  /** Trimmed range inside the parent skeleton. */
  start: number;
  end: number;
}

function trimmedSegment(s: Skel, start: number, end: number): Segment {
  let a = start;
  let b = Math.max(start, end);
  while (a < b && isWhitespace(s.text[a])) a++;
  while (b > a && isWhitespace(s.text[b - 1])) b--;
  return { skel: sliceSkel(s, a, b), start: a, end: b };
}

/**
 * R6.1 / R8.2: split a skeleton at every spaced dash into trimmed segments. With
 * `allowLeading`, a string starting with `- ` yields an empty first segment (R6.3).
 */
export function splitAtSpacedDashes(s: Skel, allowLeading = false): Segment[] {
  const segments: Segment[] = [];
  const t = s.text;
  let start = 0;
  if (allowLeading && t[0] === "-" && t[1] === " ") {
    segments.push({ skel: sliceSkel(s, 0, 0), start: 0, end: 0 });
    start = 2;
  }
  for (let i = Math.max(start, 1); i < t.length - 1; i++) {
    if (isSpacedDashAt(t, i)) {
      segments.push(trimmedSegment(s, start, i - 1));
      start = i + 2;
    }
  }
  segments.push(trimmedSegment(s, start, t.length));
  return segments;
}
