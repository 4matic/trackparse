/** R13.12 Markdown emphasis markers in a tracklist line. */
import { isWhitespace } from "../words.js";

/** Length of the maximal run of `*` or `_` at `at` (0 when there is none). */
function runLength(s: string, at: number): number {
  const ch = s[at];
  let j = at;
  if (ch === "*" || ch === "_") while (s[j] === ch) j++;
  return j - at;
}

/**
 * R13.2.1 / R13.2.3: the length of an emphasis run at `at` (a maximal run of 1 or 2 `*` or `_`),
 * else 0. Used next to the time, where runs are read by position rather than by pairing.
 */
export function emphasisRun(s: string, at: number): number {
  const n = runLength(s, at);
  return n <= 2 ? n : 0;
}

/** R13.12.2 outer side: line edge, R0.2 whitespace, a bracket, or a `*` / `_` of another run. */
const isOuter = (ch: string | undefined) =>
  ch === undefined || isWhitespace(ch) || "[]()*_".includes(ch);
/** R13.12.2 inner side: a character that is not R0.2 whitespace. */
const isInner = (ch: string | undefined) => ch !== undefined && !isWhitespace(ch);

/**
 * R13.12: `raw.slice(from)` (the line's `rest`) without its emphasis markers. A run of 1 or 2 `*`
 * or `_` closes when the character before it is an inner side and the one after it an outer side,
 * and opens the other way round. A closer pairs with the nearest opener of the same characters;
 * that opener and every opener after it leave the stack. One stack per run (`*`, `**`, `_`, `__`)
 * keeps this linear. Runs before `from` (lead and gap) never pair, except that `open`, a lead run
 * the gap left open (R13.12.4), pairs with a closing run of the same character at the end of the
 * line.
 */
export function withoutMarkers(raw: string, from: number, open?: string): string {
  const marked: boolean[] = [];
  const stacks: Record<string, number[]> = { "*": [], "**": [], _: [], __: [] };
  for (let i = from; i < raw.length; ) {
    const len = runLength(raw, i) || 1;
    const own = stacks[raw.slice(i, i + len)];
    const before = raw[i - 1];
    const after = raw[i + len];
    const at = own && isInner(before) && isOuter(after) ? own.pop() : undefined;
    if (at !== undefined) {
      for (let m = 0; m < len; m++) marked[at + m] = marked[i + m] = true;
      for (const st of Object.values(stacks)) while ((st[st.length - 1] ?? -1) > at) st.pop();
    } else if (own && isOuter(before) && isInner(after)) own.push(i);
    i += len;
  }
  // R13.12.4: the last run of the line, after which there is only R0.2 whitespace.
  let end = raw.length;
  while (end > from && isWhitespace(raw[end - 1])) end--;
  let start = end;
  while (start > from && raw[start - 1] === open) start--;
  if (
    open &&
    end - start <= 2 &&
    start > from &&
    raw[start - 1] !== open &&
    isInner(raw[start - 1])
  )
    for (let m = start; m < end && !marked[m]; m++) marked[m] = true;
  let out = "";
  for (let i = from; i < raw.length; i++) if (!marked[i]) out += raw[i];
  return out;
}
