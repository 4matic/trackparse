/**
 * Deterministic combination strategies over index vectors.
 *
 * - exhaustive: full cartesian product (lexicographic), filtered by `valid`.
 * - pairwise: greedy AETG-style covering array. Seed on the first uncovered pair,
 *   build `candidates` completions (value choice = most newly covered pairs, ties by
 *   PRNG), repair invalid completions by single-dimension changes, keep the best.
 *   Pairs no valid completion can cover are dropped (reported as `infeasible`).
 * - sample: seeded random combos, rejection-filtered, deduplicated.
 */
import type { Rng } from "./prng.js";

export type Combo = number[];

export function productSize(sizes: number[]): number {
  return sizes.reduce((a, b) => a * b, 1);
}

export function exhaustive(sizes: number[], valid: (c: Combo) => boolean): Combo[] {
  const out: Combo[] = [];
  const cur = sizes.map(() => 0);
  const n = sizes.length;
  if (sizes.some((s) => s === 0)) return out;
  for (;;) {
    if (valid(cur)) out.push([...cur]);
    let i = n - 1;
    while (i >= 0) {
      cur[i]!++;
      if (cur[i]! < sizes[i]!) break;
      cur[i] = 0;
      i--;
    }
    if (i < 0) break;
  }
  return out;
}

class PairTable {
  private readonly covered: Uint8Array[][];
  remaining = 0;
  constructor(private readonly sizes: number[]) {
    this.covered = sizes.map((si, i) =>
      sizes.map((sj, j) => {
        if (j <= i) return new Uint8Array(0);
        this.remaining += si * sj;
        return new Uint8Array(si * sj);
      }),
    );
  }
  isCovered(i: number, a: number, j: number, b: number): boolean {
    if (i > j) return this.isCovered(j, b, i, a);
    return this.covered[i]![j]![a * this.sizes[j]! + b] === 1;
  }
  mark(i: number, a: number, j: number, b: number): boolean {
    if (i > j) return this.mark(j, b, i, a);
    const arr = this.covered[i]![j]!;
    const k = a * this.sizes[j]! + b;
    if (arr[k] === 1) return false;
    arr[k] = 1;
    this.remaining--;
    return true;
  }
  /** Newly covered pair count for a full combo. */
  gain(c: Combo): number {
    let g = 0;
    for (let i = 0; i < c.length; i++)
      for (let j = i + 1; j < c.length; j++) if (!this.isCovered(i, c[i]!, j, c[j]!)) g++;
    return g;
  }
  markCombo(c: Combo): void {
    for (let i = 0; i < c.length; i++) for (let j = i + 1; j < c.length; j++) this.mark(i, c[i]!, j, c[j]!);
  }
  /** First uncovered pair in deterministic scan order, starting from a cursor. */
  firstUncovered(cursor: { i: number; j: number; k: number }): [number, number, number, number] | null {
    const n = this.sizes.length;
    for (; cursor.i < n; cursor.i++, cursor.j = cursor.i + 1, cursor.k = 0) {
      for (; cursor.j < n; cursor.j++, cursor.k = 0) {
        const arr = this.covered[cursor.i]![cursor.j]!;
        for (; cursor.k < arr.length; cursor.k++) {
          if (arr[cursor.k] === 0) {
            const sj = this.sizes[cursor.j]!;
            return [cursor.i, Math.floor(cursor.k / sj), cursor.j, cursor.k % sj];
          }
        }
      }
    }
    return null;
  }
}

export interface PairwiseResult {
  combos: Combo[];
  infeasiblePairs: number;
}

export function pairwise(
  sizes: number[],
  valid: (c: Combo) => boolean,
  rng: Rng,
  candidates: number,
): PairwiseResult {
  const n = sizes.length;
  const table = new PairTable(sizes);
  const combos: Combo[] = [];
  let infeasible = 0;
  const cursor = { i: 0, j: 1, k: 0 };
  for (;;) {
    const seed = table.firstUncovered(cursor);
    if (!seed) break;
    const [si, sa, sj, sb] = seed;
    let best: Combo | null = null;
    let bestGain = -1;
    for (let t = 0; t < candidates; t++) {
      // Seeds that stay invalid after a few repaired candidates are almost always infeasible.
      if (t >= 3 && best === null) break;
      const c: Combo = new Array<number>(n).fill(-1);
      c[si] = sa;
      c[sj] = sb;
      const order = rng.shuffle([...Array(n).keys()].filter((d) => d !== si && d !== sj));
      for (const d of order) {
        let bestV = 0;
        let bestScore = -1;
        const start = rng.int(sizes[d]!);
        for (let o = 0; o < sizes[d]!; o++) {
          const vv = (start + o) % sizes[d]!;
          let score = 0;
          for (let e = 0; e < n; e++) if (c[e]! >= 0 && e !== d && !table.isCovered(d, vv, e, c[e]!)) score++;
          if (score > bestScore) {
            bestScore = score;
            bestV = vv;
          }
        }
        c[d] = bestV;
      }
      if (!valid(c)) {
        let fixed = false;
        for (let attempt = 0; attempt < 25 && !fixed; attempt++) {
          const free = order.length ? order : [];
          if (!free.length) break;
          // change 1\u20132 free dimensions at random
          const k = 1 + rng.int(Math.min(2, free.length));
          for (let q = 0; q < k; q++) {
            const d = rng.pick(free);
            c[d] = rng.int(sizes[d]!);
          }
          fixed = valid(c);
        }
        if (!fixed) continue;
      }
      const g = table.gain(c);
      if (g > bestGain) {
        bestGain = g;
        best = [...c];
      }
    }
    if (!best) {
      table.mark(si, sa, sj, sb);
      infeasible++;
      continue;
    }
    table.markCombo(best);
    combos.push(best);
  }
  return { combos, infeasiblePairs: infeasible };
}

export function sample(
  sizes: number[],
  valid: (c: Combo) => boolean,
  rng: Rng,
  count: number,
  exclude: Set<string>,
): Combo[] {
  const out: Combo[] = [];
  const seen = new Set(exclude);
  let tries = 0;
  const maxTries = count * 200;
  while (out.length < count && tries < maxTries) {
    tries++;
    const c = sizes.map((s) => rng.int(s));
    const k = c.join(",");
    if (seen.has(k)) continue;
    if (!valid(c)) continue;
    seen.add(k);
    out.push(c);
  }
  return out;
}
