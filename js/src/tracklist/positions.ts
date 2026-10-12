/** R13.5 bare positions. */
import { digitRun } from "../digits.js";
import { isWhitespace } from "../words.js";

export interface BareCandidate {
  raw: string;
  number: number;
  /** The track part with the digits and the following space removed. */
  stripped: string;
}

/**
 * R13.5: a track part starting with 1–3 ASCII digits and a space (U+0020 only), where R4.2 took
 * no position (`positionTaken`), is a bare candidate. The remainder must be non-empty.
 */
export function bareCandidate(rest: string, positionTaken: boolean): BareCandidate | null {
  if (positionTaken) return null;
  const n = digitRun(rest, 0);
  if (n < 1 || n > 3 || rest[n] !== " ") return null;
  const stripped = rest.slice(n + 1);
  for (let i = 0; i < stripped.length; i++) {
    if (!isWhitespace(stripped[i])) {
      const raw = rest.slice(0, n);
      return { raw, number: Number(raw), stripped };
    }
  }
  return null;
}

/**
 * R13.5: candidate `i` is a position when the nearest preceding or the nearest following
 * candidate line (R13.4.2) also has a bare candidate equal to `N - 1` or `N + 1`. Linear time.
 */
export function confirmPositions(
  bare: readonly (BareCandidate | null)[],
  candidateLine: readonly boolean[],
): boolean[] {
  const n = bare.length;
  const prev: number[] = new Array(n).fill(-1);
  const next: number[] = new Array(n).fill(-1);
  let last = -1;
  for (let i = 0; i < n; i++) {
    prev[i] = last;
    if (candidateLine[i]) last = i;
  }
  last = -1;
  for (let i = n - 1; i >= 0; i--) {
    next[i] = last;
    if (candidateLine[i]) last = i;
  }
  const adjacent = (b: BareCandidate, j: number) => {
    const o = j >= 0 ? bare[j] : null;
    return o != null && (o.number === b.number - 1 || o.number === b.number + 1);
  };
  return bare.map(
    (b, i) => b !== null && (adjacent(b, prev[i] as number) || adjacent(b, next[i] as number)),
  );
}
