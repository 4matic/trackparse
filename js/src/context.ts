/** Per-call parse context: resolved options + vocabulary tables. */
import type { Tables } from "./tables.js";
import type { ParseOptions, Warning } from "./types.js";
import { dedupKey, isWhitespace } from "./words.js";

export interface Ctx {
  t: Tables;
  knownKeys: ReadonlySet<string>;
  /** Longest knownArtists entry, in UTF-16 units (bounds the R7.3a search). */
  knownMaxLen: number;
  splitAnd: "auto" | "always" | "never";
  warnings: Warning[];
  /** Counter giving every split credit list an id (R7.6 joiner inheritance across R9.2). */
  listSeq: number;
}

export function makeCtx(t: Tables, options: ParseOptions | undefined): Ctx {
  const known = (options?.knownArtists ?? []).filter((k) => typeof k === "string");
  const knownKeys = new Set(known.map(dedupKey).filter((k) => k.length > 0));
  const knownMaxLen = known.reduce((m, k) => Math.max(m, k.length), 0);
  const splitAnd = options?.splitAnd ?? "auto";
  return { t, knownKeys, knownMaxLen, splitAnd, warnings: [], listSeq: 0 };
}

export function warn(ctx: Ctx, w: Warning): void {
  if (!ctx.warnings.includes(w)) ctx.warnings.push(w);
}

/**
 * R7.3a: length of the longest knownArtists entry matching `text` at `start` (dedup-key
 * comparison), followed by end, whitespace, `,` or `;` (or `extraBoundary`: R4.2 also accepts
 * `-`, so `3 Doors Down-Kryptonite` is protected). 0 when none matches.
 */
export function matchKnownAt(text: string, start: number, ctx: Ctx, extraBoundary = ""): number {
  if (ctx.knownKeys.size === 0) return 0;
  const limit = Math.min(text.length, start + ctx.knownMaxLen * 2 + 8);
  for (let end = limit; end > start; end--) {
    const next = text[end];
    const boundary =
      end === text.length ||
      next === "," ||
      next === ";" ||
      isWhitespace(next) ||
      (extraBoundary !== "" && next === extraBoundary);
    if (!boundary) continue;
    if (ctx.knownKeys.has(dedupKey(text.slice(start, end)))) return end - start;
  }
  return 0;
}

export function isKnownArtist(text: string, ctx: Ctx): boolean {
  return ctx.knownKeys.size > 0 && ctx.knownKeys.has(dedupKey(text));
}
