/** R13.9: applyEdits. Edits apply in order, each to the result of the previous one. */
import { isWhitespace } from "../words.js";
import { hasLineBreak, joinLines, splitLines } from "./split.js";
import { formatTime, scanTime, type TimeScan } from "./times.js";
import type {
  Edit,
  EditResult,
  EditWarning,
  EditWarningCode,
  Eol,
  TracklistOptions,
} from "./types.js";

interface WorkLine {
  raw: string;
  eol: Eol;
  /** Written by an edit (R13.9.1). */
  changed: boolean;
  /** `scanTime(raw)`, kept in step with `raw` (each line is scanned once per write). */
  scan: TimeScan;
}

class InvalidEdit extends Error {}

/**
 * Edit state. Every edit validates its input before it mutates anything, so an invalid edit
 * (R13.9.6) only has to drop the warnings it added: no snapshot of the lines is needed.
 */
interface EditCtx {
  lines: WorkLine[];
  index: number;
  warnings: EditWarning[];
  /** R13.9.2: times with one-digit hours and with wider hours, over all lines. */
  oneDigit: number;
  wideHours: number;
  /** R13.9.5: how many lines end with each non-empty eol. */
  eols: Map<Eol, number>;
}

function workLine(raw: string, eol: Eol, changed: boolean): WorkLine {
  return { raw, eol, changed, scan: scanTime(raw) };
}

/** Add (`by` = 1) or remove (`by` = -1) a line's time from the hour-width counts (R13.9.2). */
function countHours(ctx: EditCtx, line: WorkLine, by: 1 | -1): void {
  const style = line.scan.style;
  if (!style?.hours) return;
  if (style.hourWidth === 1) ctx.oneDigit += by;
  else ctx.wideHours += by;
}

function countEol(ctx: EditCtx, eol: Eol, by: 1 | -1): void {
  if (eol !== "") ctx.eols.set(eol, (ctx.eols.get(eol) ?? 0) + by);
}

function setEol(ctx: EditCtx, line: WorkLine, eol: Eol): void {
  countEol(ctx, line.eol, -1);
  line.eol = eol;
  line.changed = true;
  countEol(ctx, eol, 1);
}

/** A line enters (`by` = 1) or leaves (`by` = -1) the text. */
function countLine(ctx: EditCtx, line: WorkLine, by: 1 | -1): void {
  countHours(ctx, line, by);
  countEol(ctx, line.eol, by);
}

function setRaw(ctx: EditCtx, line: WorkLine, raw: string): void {
  countHours(ctx, line, -1);
  line.raw = raw;
  line.scan = scanTime(raw);
  line.changed = true;
  countHours(ctx, line, 1);
}

function warnEdit(ctx: EditCtx, code: EditWarningCode, line: number | null): void {
  ctx.warnings.push({ code, edit: ctx.index, line });
}

function field(edit: object, key: string): unknown {
  return (edit as Record<string, unknown>)[key];
}

function has(edit: object, key: string): boolean {
  return field(edit, key) !== undefined;
}

/** 0-based index of an existing 1-based line number, else InvalidEdit. */
function lineIndex(ctx: EditCtx, value: unknown): number {
  if (typeof value !== "number" || !Number.isInteger(value)) throw new InvalidEdit();
  if (value < 1 || value > ctx.lines.length) throw new InvalidEdit();
  return value - 1;
}

function finite(value: unknown): number {
  if (typeof value !== "number" || !Number.isFinite(value)) throw new InvalidEdit();
  return value;
}

function singleLine(value: unknown): string {
  if (typeof value !== "string" || hasLineBreak(value)) throw new InvalidEdit();
  return value;
}

/** R13.9.2: do the text's times with hours use one-digit hours? (All of them, at least one.) */
function oneDigitHours(ctx: EditCtx): boolean {
  return ctx.oneDigit > 0 && ctx.wideHours === 0;
}

interface Retime {
  index: number;
  raw: string;
  clamped: boolean;
  upgraded: boolean;
}

/** R13.9.2 / R13.9.3: the rewritten line for a new value, or InvalidEdit. */
function retime(ctx: EditCtx, index: number, value: number, oneDigit: boolean): Retime {
  const scan = (ctx.lines[index] as WorkLine).scan;
  if (!scan.style || !scan.time) throw new InvalidEdit();
  const clamped = value < 0;
  const f = formatTime(clamped ? 0 : value, scan.style, oneDigit);
  if (!f) throw new InvalidEdit();
  const p = scan.parts;
  return { index, raw: p.lead + f.raw + p.gap + p.rest, clamped, upgraded: f.upgraded };
}

function commit(ctx: EditCtx, changes: readonly Retime[]): void {
  for (const c of changes) {
    setRaw(ctx, ctx.lines[c.index] as WorkLine, c.raw);
    if (c.clamped) warnEdit(ctx, "time-clamped", c.index + 1);
    if (c.upgraded) warnEdit(ctx, "style-upgraded", c.index + 1);
  }
}

/** Lines whose neighbourhood `canonical` must check after an edit; null = every line. */
type Touched = number[] | null;

function setTime(ctx: EditCtx, edit: object): Touched {
  const index = lineIndex(ctx, field(edit, "line"));
  const value = finite(field(edit, "seconds"));
  commit(ctx, [retime(ctx, index, value, oneDigitHours(ctx))]);
  return []; // a timed line is never empty and keeps its eol
}

function shift(ctx: EditCtx, edit: object): Touched {
  const delta = finite(field(edit, "seconds"));
  let targets: number[];
  if (has(edit, "lines")) {
    const list = field(edit, "lines");
    if (!Array.isArray(list)) throw new InvalidEdit();
    const set = new Set(list.map((v) => lineIndex(ctx, v)));
    targets = [...set].sort((a, b) => a - b);
  } else {
    const from = has(edit, "from") ? lineIndex(ctx, field(edit, "from")) : 0;
    targets = [];
    for (let i = from; i < ctx.lines.length; i++) targets.push(i);
  }
  const oneDigit = oneDigitHours(ctx);
  const changes: Retime[] = [];
  for (const i of targets) {
    const time = (ctx.lines[i] as WorkLine).scan.time;
    if (time) changes.push(retime(ctx, i, time.seconds + delta, oneDigit));
  }
  commit(ctx, changes);
  return [];
}

/** R13.9.4: stable sort of blocks (a timed line plus the untimed lines after it). */
function sortByTime(ctx: EditCtx): Touched {
  const lines = ctx.lines;
  const first = lines.findIndex((l) => l.scan.time !== null);
  if (first < 0) return [];
  const blocks: { seconds: number; from: number; to: number }[] = [];
  for (let i = first; i < lines.length; i++) {
    const t = (lines[i] as WorkLine).scan.time;
    if (t) blocks.push({ seconds: t.seconds, from: i, to: i + 1 });
    else (blocks[blocks.length - 1] as { to: number }).to = i + 1;
  }
  const sorted = blocks
    .map((b, k) => ({ b, k }))
    .sort((x, y) => x.b.seconds - y.b.seconds || x.k - y.k)
    .map((x) => x.b);
  const order: number[] = [];
  for (let i = 0; i < first; i++) order.push(i);
  for (const b of sorted) for (let i = b.from; i < b.to; i++) order.push(i);
  const old = ctx.lines;
  ctx.lines = order.map((src, slot) => {
    const line = old[src] as WorkLine;
    const eol = (old[slot] as WorkLine).eol; // eol values stay in their slots
    return { raw: line.raw, eol, changed: line.changed || src !== slot, scan: line.scan };
  });
  return null;
}

function setRest(ctx: EditCtx, edit: object): Touched {
  const index = lineIndex(ctx, field(edit, "line"));
  const rest = singleLine(field(edit, "rest"));
  const line = ctx.lines[index] as WorkLine;
  const p = line.scan.parts;
  // R13.9 set-rest: a non-empty rest after a time is preceded by whitespace; a gap that does
  // not end with R0.2 whitespace (`""`, `]`, `)`) gets one space.
  const spaced = p.gap.length > 0 && isWhitespace(p.gap[p.gap.length - 1]);
  const gap = p.time !== "" && !spaced && rest !== "" ? `${p.gap} ` : p.gap;
  setRaw(ctx, line, p.lead + p.time + gap + rest);
  return [index];
}

/**
 * R13.9.5: the most common non-empty eol (ties: the one seen first in line order), `"\n"` if
 * none. Counts are kept up to date; only a tie needs a look at the lines.
 */
function commonEol(ctx: EditCtx): Eol {
  let best = 0;
  const top: Eol[] = [];
  for (const [eol, n] of ctx.eols) {
    if (n <= 0 || n < best) continue;
    if (n > best) top.length = 0;
    best = n;
    top.push(eol);
  }
  if (top.length === 0) return "\n";
  if (top.length === 1) return top[0] as Eol;
  for (const l of ctx.lines) if (top.includes(l.eol)) return l.eol;
  return "\n";
}

function insert(ctx: EditCtx, edit: object): Touched {
  const after = field(edit, "after");
  if (typeof after !== "number" || !Number.isInteger(after)) throw new InvalidEdit();
  if (after < 0 || after > ctx.lines.length) throw new InvalidEdit();
  const text = singleLine(field(edit, "text"));
  const eol = commonEol(ctx);
  const last = ctx.lines[ctx.lines.length - 1];
  // R13.9.5: inserting into an empty text keeps it without a trailing newline.
  const line = workLine(text, ctx.lines.length === 0 ? "" : eol, true);
  if (after === ctx.lines.length && last && last.eol === "") {
    setEol(ctx, last, eol);
    line.eol = "";
  }
  ctx.lines.splice(after, 0, line);
  countLine(ctx, line, 1);
  return [after];
}

function remove(ctx: EditCtx, edit: object): Touched {
  const index = lineIndex(ctx, field(edit, "line"));
  const [gone] = ctx.lines.splice(index, 1);
  if (gone) countLine(ctx, gone, -1);
  const last = ctx.lines[ctx.lines.length - 1];
  if (gone && index === ctx.lines.length && last && last.eol !== gone.eol) {
    setEol(ctx, last, gone.eol); // R13.9.5: the deleted last line's eol moves up
  }
  return [index];
}

/** A `"\r"` line followed by an empty `"\n"` line reads back as one CRLF line. */
function mergeAt(ctx: EditCtx, k: number): void {
  const prev = ctx.lines[k];
  const next = ctx.lines[k + 1];
  if (!prev || !next || prev.eol !== "\r" || next.raw !== "" || next.eol !== "\n") return;
  setEol(ctx, prev, "\r\n");
  ctx.lines.splice(k + 1, 1);
  countLine(ctx, next, -1);
}

/**
 * R13.9.5: keep the work lines equal to what `splitLines(join(lines))` would give: a `"\r"`
 * followed by an empty `"\n"` line reads back as one CRLF, and an empty last line without eol is
 * not a line. The lines are canonical before each edit, so only the neighbourhood of the lines
 * the edit touched (and the end) is checked; `null` checks every line (sort-by-time).
 */
function canonical(ctx: EditCtx, touched: Touched): void {
  const lines = ctx.lines;
  let starts: number[];
  if (touched === null) {
    starts = [];
    for (let k = lines.length - 2; k >= 0; k--) starts.push(k);
  } else {
    const set = new Set<number>([lines.length - 2]);
    for (const t of touched) {
      set.add(t - 1);
      set.add(t);
    }
    starts = [...set].sort((a, b) => b - a);
  }
  // Descending, so a merge at k never moves a pair still to be checked.
  for (const k of starts) mergeAt(ctx, k);
  const last = lines[lines.length - 1];
  if (last && last.raw === "" && last.eol === "") {
    lines.pop();
    countLine(ctx, last, -1);
  }
}

function applyOne(ctx: EditCtx, edit: unknown): Touched {
  if (typeof edit !== "object" || edit === null || Array.isArray(edit)) throw new InvalidEdit();
  const op = field(edit, "op");
  if (op === "set-time") return setTime(ctx, edit);
  if (op === "shift") return shift(ctx, edit);
  if (op === "sort-by-time") return sortByTime(ctx);
  if (op === "set-rest") return setRest(ctx, edit);
  if (op === "insert") return insert(ctx, edit);
  if (op === "delete") return remove(ctx, edit);
  throw new InvalidEdit();
}

/** R13.9.6: an `edit-invalid` warning's line is the edit's integer `line`, else `after`, else null. */
function invalidLine(edit: unknown): number | null {
  if (typeof edit !== "object" || edit === null) return null;
  try {
    for (const key of ["line", "after"]) {
      const value = field(edit, key);
      if (typeof value === "number" && Number.isInteger(value)) return value;
    }
    return null;
  } catch {
    return null;
  }
}

/**
 * R13.9: apply `edits` to `text`. Never throws (R13.9.6): an invalid edit changes nothing and
 * warns `edit-invalid`.
 */
export function applyEdits(
  text: string,
  edits: readonly Edit[],
  options?: TracklistOptions,
): EditResult {
  void options; // R13.10: accepted for symmetry; edits do not depend on parse options.
  const source = typeof text === "string" ? text : "";
  const list: readonly unknown[] = Array.isArray(edits) ? edits : [];
  if (list.length === 0) return { text: source, changed: [], warnings: [] };
  const ctx: EditCtx = {
    lines: splitLines(source).map((l) => workLine(l.raw, l.eol, false)),
    index: 0,
    warnings: [],
    oneDigit: 0,
    wideHours: 0,
    eols: new Map(),
  };
  for (const l of ctx.lines) countLine(ctx, l, 1);
  for (let i = 0; i < list.length; i++) {
    ctx.index = i;
    const warningsBefore = ctx.warnings.length;
    try {
      // Validation throws before any mutation, so the lines need no rollback (R13.9.6).
      canonical(ctx, applyOne(ctx, list[i]));
    } catch {
      ctx.warnings.length = warningsBefore;
      warnEdit(ctx, "edit-invalid", invalidLine(list[i]));
    }
  }
  const changed: number[] = [];
  ctx.lines.forEach((l, i) => {
    if (l.changed) changed.push(i + 1);
  });
  return { text: joinLines(ctx.lines), changed, warnings: ctx.warnings };
}
