/** R13.9: applyEdits. Edits apply in order, each to the result of the previous one. */
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
}

class InvalidEdit extends Error {}

interface EditCtx {
  lines: WorkLine[];
  index: number;
  warnings: EditWarning[];
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
function oneDigitHours(scans: readonly TimeScan[]): boolean {
  let one = 0;
  let more = 0;
  for (const s of scans) {
    if (!s.style?.hours) continue;
    if (s.style.hourWidth === 1) one++;
    else more++;
  }
  return one > 0 && more === 0;
}

interface Retime {
  index: number;
  raw: string;
  clamped: boolean;
  upgraded: boolean;
}

/** R13.9.2 / R13.9.3: the rewritten line for a new value, or InvalidEdit. */
function retime(
  index: number,
  value: number,
  scans: readonly TimeScan[],
  oneDigit: boolean,
): Retime {
  const scan = scans[index] as TimeScan;
  if (!scan.style || !scan.time) throw new InvalidEdit();
  const clamped = value < 0;
  const f = formatTime(clamped ? 0 : value, scan.style, oneDigit);
  if (!f) throw new InvalidEdit();
  const p = scan.parts;
  return { index, raw: p.lead + f.raw + p.gap + p.rest, clamped, upgraded: f.upgraded };
}

function commit(ctx: EditCtx, changes: readonly Retime[]): void {
  for (const c of changes) {
    const line = ctx.lines[c.index] as WorkLine;
    line.raw = c.raw;
    line.changed = true;
    if (c.clamped) warnEdit(ctx, "time-clamped", c.index + 1);
    if (c.upgraded) warnEdit(ctx, "style-upgraded", c.index + 1);
  }
}

function scansOf(lines: readonly WorkLine[]): TimeScan[] {
  return lines.map((l) => scanTime(l.raw));
}

function setTime(ctx: EditCtx, edit: object): void {
  const index = lineIndex(ctx, field(edit, "line"));
  const value = finite(field(edit, "seconds"));
  const scans = scansOf(ctx.lines);
  commit(ctx, [retime(index, value, scans, oneDigitHours(scans))]);
}

function shift(ctx: EditCtx, edit: object): void {
  const delta = finite(field(edit, "seconds"));
  const scans = scansOf(ctx.lines);
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
  const oneDigit = oneDigitHours(scans);
  const changes: Retime[] = [];
  for (const i of targets) {
    const time = (scans[i] as TimeScan).time;
    if (time) changes.push(retime(i, time.seconds + delta, scans, oneDigit));
  }
  commit(ctx, changes);
}

/** R13.9.4: stable sort of blocks (a timed line plus the untimed lines after it). */
function sortByTime(ctx: EditCtx): void {
  const scans = scansOf(ctx.lines);
  const first = scans.findIndex((s) => s.time !== null);
  if (first < 0) return;
  const blocks: { seconds: number; from: number; to: number }[] = [];
  for (let i = first; i < scans.length; i++) {
    const t = (scans[i] as TimeScan).time;
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
    return { raw: line.raw, eol, changed: line.changed || src !== slot };
  });
}

function setRest(ctx: EditCtx, edit: object): void {
  const index = lineIndex(ctx, field(edit, "line"));
  const rest = singleLine(field(edit, "rest"));
  const line = ctx.lines[index] as WorkLine;
  const p = scanTime(line.raw).parts;
  line.raw = p.lead + p.time + p.gap + rest;
  line.changed = true;
}

/** R13.9.5: the most common non-empty eol (ties: the first seen), `"\n"` if none. */
function commonEol(lines: readonly WorkLine[]): Eol {
  const counts = new Map<Eol, number>();
  for (const l of lines) if (l.eol !== "") counts.set(l.eol, (counts.get(l.eol) ?? 0) + 1);
  let best: Eol = "\n";
  let bestCount = 0;
  // Map iteration follows first-seen order, so `>` keeps the first eol among equal counts.
  for (const [eol, n] of counts) {
    if (n > bestCount) {
      best = eol;
      bestCount = n;
    }
  }
  return best;
}

function insert(ctx: EditCtx, edit: object): void {
  const after = field(edit, "after");
  if (typeof after !== "number" || !Number.isInteger(after)) throw new InvalidEdit();
  if (after < 0 || after > ctx.lines.length) throw new InvalidEdit();
  const text = singleLine(field(edit, "text"));
  const eol = commonEol(ctx.lines);
  const last = ctx.lines[ctx.lines.length - 1];
  // R13.9.5: inserting into an empty text keeps it without a trailing newline.
  const line: WorkLine = { raw: text, eol: ctx.lines.length === 0 ? "" : eol, changed: true };
  if (after === ctx.lines.length && last && last.eol === "") {
    last.eol = eol;
    last.changed = true;
    line.eol = "";
  }
  ctx.lines.splice(after, 0, line);
}

function remove(ctx: EditCtx, edit: object): void {
  const index = lineIndex(ctx, field(edit, "line"));
  const [gone] = ctx.lines.splice(index, 1);
  const last = ctx.lines[ctx.lines.length - 1];
  if (gone && index === ctx.lines.length && last && last.eol !== gone.eol) {
    last.eol = gone.eol; // R13.9.5: the deleted last line's eol moves up
    last.changed = true;
  }
}

/**
 * Keep the work lines equal to what `splitLines(join(lines))` would give: a `"\r"` followed by an
 * empty `"\n"` line reads back as one CRLF, and an empty last line without eol is not a line.
 */
function canonical(lines: WorkLine[]): WorkLine[] {
  const out: WorkLine[] = [];
  for (const l of lines) {
    const prev = out[out.length - 1];
    if (prev && prev.eol === "\r" && l.raw === "" && l.eol === "\n") {
      prev.eol = "\r\n";
      prev.changed = true;
      continue;
    }
    out.push(l);
  }
  const last = out[out.length - 1];
  if (last && last.raw === "" && last.eol === "") out.pop();
  return out;
}

function applyOne(ctx: EditCtx, edit: unknown): void {
  if (typeof edit !== "object" || edit === null || Array.isArray(edit)) throw new InvalidEdit();
  const op = field(edit, "op");
  if (op === "set-time") setTime(ctx, edit);
  else if (op === "shift") shift(ctx, edit);
  else if (op === "sort-by-time") sortByTime(ctx);
  else if (op === "set-rest") setRest(ctx, edit);
  else if (op === "insert") insert(ctx, edit);
  else if (op === "delete") remove(ctx, edit);
  else throw new InvalidEdit();
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

function snapshot(lines: readonly WorkLine[]): WorkLine[] {
  return lines.map((l) => ({ ...l }));
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
  const ctx: EditCtx = {
    lines: splitLines(source).map((l) => ({ ...l, changed: false })),
    index: 0,
    warnings: [],
  };
  for (let i = 0; i < list.length; i++) {
    ctx.index = i;
    const before = snapshot(ctx.lines);
    const warningsBefore = ctx.warnings.length;
    try {
      applyOne(ctx, list[i]);
      ctx.lines = canonical(ctx.lines);
    } catch {
      ctx.lines = before;
      ctx.warnings.length = warningsBefore;
      warnEdit(ctx, "edit-invalid", invalidLine(list[i]));
    }
  }
  const changed: number[] = [];
  ctx.lines.forEach((l, i) => {
    if (l.changed) changed.push(i + 1);
  });
  if (list.length === 0) return { text: source, changed: [], warnings: [] };
  return { text: joinLines(ctx.lines), changed, warnings: ctx.warnings };
}
