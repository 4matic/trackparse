import { isWhitespaceCode } from "./words.js";

/** R1.2 invisible characters removed outright. ZWNJ/ZWJ (U+200C/U+200D) are kept (ZWJ emoji). */
// R1.2: U+E000 is reserved as the R3.4 group placeholder, so it can never occur in input.
const REMOVED = new Set([0x200b, 0x2060, 0xfeff, 0x00ad, 0xe000]);

/** R1.3 brackets, R1.4 quotes, R1.5 dashes → ASCII. */
const CHAR_MAP = new Map<number, string>([
  [0xff08, "("],
  [0xff09, ")"],
  [0xff3b, "["],
  [0x3010, "["],
  [0x3014, "["],
  [0xff3d, "]"],
  [0x3011, "]"],
  [0x3015, "]"],
  [0xff5b, "{"],
  [0xff5d, "}"],
  [0x201c, '"'],
  [0x201d, '"'],
  [0x201e, '"'],
  [0x00ab, '"'],
  [0x00bb, '"'],
  [0x300c, '"'],
  [0x300d, '"'],
  [0x300e, '"'],
  [0x300f, '"'],
  [0xff02, '"'],
  [0x2018, "'"],
  [0x2019, "'"],
  [0x201a, "'"],
  [0x0060, "'"],
  [0x00b4, "'"],
  [0x2212, "-"],
  [0xfe58, "-"],
  [0xfe63, "-"],
  [0xff0d, "-"],
]);

function mapChar(cp: number): string | null {
  if (REMOVED.has(cp)) return null;
  if (cp >= 0x2010 && cp <= 0x2015) return "-"; // R1.5
  return CHAR_MAP.get(cp) ?? String.fromCodePoint(cp);
}

/** R1.5 (second half): a run of 2+ `-` with whitespace on both sides becomes one `-`. */
function collapseSpacedDashRuns(chars: string[]): string[] {
  const out: string[] = [];
  let i = 0;
  while (i < chars.length) {
    if (chars[i] !== "-") {
      out.push(chars[i] as string);
      i++;
      continue;
    }
    let j = i;
    while (chars[j] === "-") j++;
    const before = chars[i - 1];
    const after = chars[j];
    const spaced =
      j - i >= 2 &&
      before !== undefined &&
      after !== undefined &&
      isWhitespaceCode(before.codePointAt(0) ?? 0) &&
      isWhitespaceCode(after.codePointAt(0) ?? 0);
    if (spaced) out.push("-");
    else for (let k = i; k < j; k++) out.push("-");
    i = j;
  }
  return out;
}

/** R1.6: every whitespace code point → one space, runs collapsed, trimmed. */
function collapseWhitespace(chars: string[]): string {
  let out = "";
  let pending = false;
  for (const ch of chars) {
    if (isWhitespaceCode(ch.codePointAt(0) ?? 0)) {
      pending = out.length > 0;
      continue;
    }
    if (pending) out += " ";
    pending = false;
    out += ch;
  }
  return out;
}

/** R1: normalize a track string. Idempotent; never throws on string input. */
export function normalize(input: string): string {
  if (typeof input !== "string") return "";
  const chars: string[] = [];
  for (const ch of input.normalize("NFC")) {
    const mapped = mapChar(ch.codePointAt(0) ?? 0);
    if (mapped !== null) chars.push(mapped);
  }
  // R1.8: removing invisibles can leave a decomposed sequence behind; re-compose so that
  // normalize() stays idempotent.
  return collapseWhitespace(collapseSpacedDashRuns(chars)).normalize("NFC");
}
