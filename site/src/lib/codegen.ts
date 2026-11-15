// The code panels of the playground: the same calls in JavaScript, Python, Rust, Go and C, laid
// out as each language's formatter prints them (Biome at 100 columns, Ruff at 100 columns,
// rustfmt, gofmt and clang-format at 100 columns), plus a link to the API docs of every function
// and type a snippet uses.
// Pure functions: the playground passes in what its controls hold.

import type { Lang } from "./lang";

/** A generated program or snippet, and the API names it uses, in order of first use. */
export interface Snippet {
  src: string;
  api: string[];
}

/** The parse options the playground can set. */
export interface TrackOptions {
  mode?: string;
  splitAnd?: string;
  knownArtists?: string[];
  uploader?: string;
}

/** The format options the playground can set (only the ones that differ from the default). */
export interface FormatChoice {
  feat?: string;
  joiners?: string;
  versions?: string;
}

/** What the tracklist panel shows in code: the text, its options and the edit inputs. */
export interface ListInput {
  text: string;
  audioSeconds?: number;
  knownArtists?: string[];
  seconds: number;
  from: number;
  /** The suggested fix of the first warning that has one. */
  fix?: { line: number; seconds: number };
}

const WIDTH = 100;

// ---- shared -------------------------------------------------------------------------------------

/** Control, invisible and bidi characters, written as escapes in the generated literals. */
function invisible(cp: number): boolean {
  return (
    cp < 0x20 ||
    (cp >= 0x7f && cp <= 0x9f) ||
    cp === 0xad ||
    cp === 0x34f ||
    cp === 0x61c ||
    cp === 0x180e ||
    (cp >= 0x200b && cp <= 0x200f) ||
    (cp >= 0x2028 && cp <= 0x202e) ||
    (cp >= 0x2060 && cp <= 0x206f) ||
    cp === 0xfeff ||
    (cp >= 0xfff9 && cp <= 0xfffb)
  );
}

/** Columns a line takes, as the formatters count them: wide characters (emoji, CJK) count two. */
export function cols(s: string): number {
  let w = 0;
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    const wide =
      (cp >= 0x1100 && cp <= 0x115f) ||
      (cp >= 0x2e80 && cp <= 0xa4cf) ||
      (cp >= 0xac00 && cp <= 0xd7a3) ||
      (cp >= 0xf900 && cp <= 0xfaff) ||
      (cp >= 0xfe30 && cp <= 0xfe4f) ||
      (cp >= 0xff00 && cp <= 0xff60) ||
      (cp >= 0xffe0 && cp <= 0xffe6) ||
      (cp >= 0x1f300 && cp <= 0x1faff) ||
      cp >= 0x20000;
    w += wide ? 2 : 1;
  }
  return w;
}
/** The widest line of a block of text. */
const widest = (s: string) => Math.max(...s.split("\n").map(cols));

/**
 * A JavaScript or Python string literal. Double quotes, unless the string holds more double
 * quotes than single ones (Biome, Prettier and Ruff all choose that way). Control, invisible
 * and bidi characters, and lone surrogates, are written as `\uXXXX`.
 */
export function strLit(s: string): string {
  let dq = 0;
  let sq = 0;
  for (const ch of s) {
    if (ch === '"') dq++;
    else if (ch === "'") sq++;
  }
  const quote = dq > sq ? "'" : '"';
  let out = quote;
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    if (ch === quote || ch === "\\") out += `\\${ch}`;
    else if (ch === "\n") out += "\\n";
    else if (ch === "\r") out += "\\r";
    else if (ch === "\t") out += "\\t";
    else if (invisible(cp) || (cp >= 0xd800 && cp <= 0xdfff)) out += `\\u${cp.toString(16).padStart(4, "0")}`;
    else out += ch;
  }
  return out + quote;
}

/** A number as JavaScript and Python print it in source: `1e21`, not `1e+21`. */
const numLit = (n: number) => String(n).replace("e+", "e");

// ---- JavaScript ---------------------------------------------------------------------------------

/** A variable name in generated code, where a value would be written as a literal. */
const IDENT = Symbol("ident");
interface Ident {
  [IDENT]: string;
}
const ident = (name: string): Ident => ({ [IDENT]: name });
const isIdent = (v: Value): v is Ident => typeof v === "object" && IDENT in v;
type Value = string | number | boolean | Ident | Value[] | { [key: string]: Value };
const composite = (v: Value): v is Value[] | { [key: string]: Value } => typeof v === "object" && !isIdent(v);

function jsFlat(v: Value): string {
  if (isIdent(v)) return v[IDENT];
  if (Array.isArray(v)) return `[${v.map(jsFlat).join(", ")}]`;
  if (typeof v === "object") {
    const e = Object.entries(v);
    return e.length ? `{ ${e.map(([k, x]) => `${k}: ${jsFlat(x)}`).join(", ")} }` : "{}";
  }
  return typeof v === "string" ? strLit(v) : typeof v === "number" ? numLit(v) : String(v);
}

/** `v` after `before` on a line that ends with `after`: flat if it fits, else one item per line. */
function jsLay(v: Value, indent: string, before: string, after: string): string {
  const flat = jsFlat(v);
  if (!composite(v) || cols(before + flat + after) <= WIDTH) return flat;
  const inner = `${indent}  `;
  if (Array.isArray(v)) {
    if (!v.length) return flat;
    return `[\n${v.map((x) => `${inner}${jsLay(x, inner, inner, ",")},`).join("\n")}\n${indent}]`;
  }
  const e = Object.entries(v);
  if (!e.length) return flat;
  return `{\n${e.map(([k, x]) => `${inner}${k}: ${jsLay(x, inner, `${inner}${k}: `, ",")},`).join("\n")}\n${indent}}`;
}

/** `lhs fn(args);` as Biome prints it: flat, else the last object hugged, else one argument per line. */
function jsCall(lhs: string, fn: string, args: Value[]): string {
  const flat = `${lhs}${fn}(${args.map(jsFlat).join(", ")});`;
  if (cols(flat) <= WIDTH) return flat;
  const last = args[args.length - 1];
  const head = `${lhs}${fn}(${args.slice(0, -1).map((a) => `${jsFlat(a)}, `).join("")}`;
  if (composite(last) && cols(`${head}{`) <= WIDTH) return `${head}${jsLay(last, "", head, ");")});`;
  return `${lhs}${fn}(\n${args.map((a) => `  ${jsLay(a, "  ", "  ", ",")},`).join("\n")}\n);`;
}

// ---- Python -------------------------------------------------------------------------------------

function pyFlat(v: Value): string {
  if (isIdent(v)) return v[IDENT];
  if (Array.isArray(v)) return `[${v.map(pyFlat).join(", ")}]`;
  if (typeof v === "object") return `{${Object.entries(v).map(([k, x]) => `${strLit(k)}: ${pyFlat(x)}`).join(", ")}}`;
  if (typeof v === "boolean") return v ? "True" : "False";
  return typeof v === "string" ? strLit(v) : numLit(v);
}

/** A list after `before` on a line that ends with `after`: flat if it fits, else one item per line. */
function pyLay(v: Value, indent: string, before: string, after: string): string {
  const flat = pyFlat(v);
  if (!Array.isArray(v) || !v.length || cols(before + flat + after) <= WIDTH) return flat;
  const inner = `${indent}    `;
  return `[\n${v.map((x) => `${inner}${pyLay(x, inner, inner, ",")},`).join("\n")}\n${indent}]`;
}

/**
 * `lhs fn(args)` as Ruff prints it: flat; else the arguments together on one indented line
 * (or a lone argument that can't be split); else one argument per line with a trailing comma.
 */
function pyCall(lhs: string, fn: string, args: [string | null, Value][]): string {
  const parts = args.map(([k, v]) => `${k ? `${k}=` : ""}${pyFlat(v)}`);
  const flat = `${lhs}${fn}(${parts.join(", ")})`;
  if (cols(flat) <= WIDTH) return flat;
  const lone = args.length === 1 && !composite(args[0][1]);
  if (lone || cols(`    ${parts.join(", ")}`) <= WIDTH) return `${lhs}${fn}(\n    ${parts.join(", ")}\n)`;
  const lines = args.map(([k, v]) => {
    const pre = `    ${k ? `${k}=` : ""}`;
    return `${pre}${pyLay(v, "    ", pre, ",")},`;
  });
  return `${lhs}${fn}(\n${lines.join("\n")}\n)`;
}

// ---- Rust ---------------------------------------------------------------------------------------

/** A Rust string literal. Control, invisible and bidi characters are written as `\u{…}`. */
export function rsStr(s: string): string {
  let out = '"';
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    if (ch === '"' || ch === "\\") out += `\\${ch}`;
    else if (ch === "\n") out += "\\n";
    else if (ch === "\r") out += "\\r";
    else if (ch === "\t") out += "\\t";
    // A lone surrogate cannot be in a Rust string: U+FFFD, as a lossy conversion would give.
    else if (cp >= 0xd800 && cp <= 0xdfff) out += "\\u{fffd}";
    else if (invisible(cp)) out += `\\u{${cp.toString(16)}}`;
    else out += ch;
  }
  return `${out}"`;
}
/** An `f64` literal: `45.0`, `7.5`, `1e21`. */
function rsNum(n: number): string {
  if (!Number.isFinite(n)) return Number.isNaN(n) ? "f64::NAN" : n > 0 ? "f64::INFINITY" : "f64::NEG_INFINITY";
  const s = String(n).replace("e+", "e");
  return /[.e]/.test(s) ? s : `${s}.0`;
}
/** `youtube` → `Youtube`: the enum variant of an option value. */
const variant = (v: string) => v.charAt(0).toUpperCase() + v.slice(1);

/** `use` items in rustfmt order: functions first, then types. */
function rsUse(path: string, names: string[]): string {
  const lower = (n: string) => n[0] === n[0].toLowerCase();
  const sorted = [...names.filter(lower).sort(), ...names.filter((n) => !lower(n)).sort()];
  const line = `use ${path}::${sorted.length === 1 ? sorted[0] : `{${sorted.join(", ")}}`};`;
  if (line.length <= WIDTH) return line;
  // Too long: the names fill lines of at most 100 columns inside braces.
  const rows: string[] = [];
  for (const n of sorted) {
    const last = rows.length - 1;
    if (last >= 0 && `${rows[last]} ${n},`.length <= WIDTH) rows[last] += ` ${n},`;
    else rows.push(`    ${n},`);
  }
  return `use ${path}::{\n${rows.join("\n")}\n};`;
}

/**
 * An array argument, `.known_artists([…])` at `indent`: on one line up to 60 columns
 * (rustfmt's array width), else short items (10 columns at most) fill lines and longer ones
 * get a line each.
 */
function rsArrayCall(method: string, items: string[], indent: string): string {
  const flat = `[${items.join(", ")}]`;
  if (cols(flat) <= 60) return `${method}(${flat})`;
  const inner = `${indent}    `;
  if (items.every((x) => cols(x) <= 10)) {
    const rows: string[] = [];
    for (const x of items) {
      const last = rows.length - 1;
      if (last >= 0 && cols(`${rows[last]} ${x},`) <= WIDTH) rows[last] += ` ${x},`;
      else rows.push(`${inner}${x},`);
    }
    return `${method}([\n${rows.join("\n")}\n${indent}])`;
  }
  return `${method}([\n${items.map((x) => `${inner}${x},`).join("\n")}\n${indent}])`;
}

/**
 * `let x = Root::new().a().b();`: on one line when the chain is at most 60 columns and every call
 * is on one line, else one call per line. `calls` render each call at a given indent.
 */
function rsChain(lhs: string, root: string, calls: ((indent: string) => string)[], indent = "    "): string {
  const one = calls.map((c) => c(indent));
  const chain = root + one.join("");
  if (!chain.includes("\n") && (calls.length < 2 || cols(chain) <= 60) && cols(`${indent}${lhs}${chain};`) <= WIDTH)
    return `${lhs}${chain};`;
  if (calls.length === 1) return `${lhs}${chain};`;
  // The last call may run over several lines (a long array) when the chain up to its first line
  // is on one line and at most 60 columns.
  const init = one.slice(0, -1).join("");
  const last = one[one.length - 1];
  if (!init.includes("\n") && last.includes("\n") && cols(`${root}${init}${last.split("\n")[0]}`) <= 60)
    return `${lhs}${chain};`;
  return `${lhs}${root}\n${calls.map((c) => `${indent}    ${c(`${indent}    `)}`).join("\n")};`;
}

/** `let x = f(a, b);`: arguments wider than 60 columns go one per line, if each then fits. */
function rsCall(lhs: string, fn: string, args: string[], indent = "    "): string {
  const flat = `${lhs}${fn}(${args.join(", ")});`;
  const fits = args.every((a) => indent.length + 4 + cols(a) + 1 <= WIDTH);
  // One string argument stays on the line as long as the line fits.
  const narrow = args.length === 1 ? cols(indent + flat) <= WIDTH : cols(args.join(", ")) <= 60;
  if (narrow || !fits) return flat;
  return `${lhs}${fn}(\n${args.map((a) => `${indent}    ${a},`).join("\n")}\n${indent});`;
}

// ---- Go -----------------------------------------------------------------------------------------

/** A Go string literal. Control, invisible and bidi characters are written as escapes. */
export function goStr(s: string): string {
  let out = '"';
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    if (ch === '"' || ch === "\\") out += `\\${ch}`;
    else if (ch === "\n") out += "\\n";
    else if (ch === "\r") out += "\\r";
    else if (ch === "\t") out += "\\t";
    // A Go string cannot spell a lone surrogate: U+FFFD, as the module reads invalid UTF-8.
    else if (cp >= 0xd800 && cp <= 0xdfff) out += "\\ufffd";
    else if (invisible(cp)) out += `\\u${cp.toString(16).padStart(4, "0")}`;
    else out += ch;
  }
  return `${out}"`;
}
/** A float64 constant: `45`, `7.5`, `1e+21`; `math.Inf(1)` and `math.NaN()` need the math import. */
function goNum(n: number): string {
  if (!Number.isFinite(n)) return Number.isNaN(n) ? "math.NaN()" : n > 0 ? "math.Inf(1)" : "math.Inf(-1)";
  return String(n);
}
/** A float64 value for trackparse.Ptr: `3600.0`, `7.5`, so the type parameter is float64, not int. */
function goFloat(n: number): string {
  const s = goNum(n);
  return /^-?\d+$/.test(s) ? `${s}.0` : s;
}
/** The exported constant of an option value: `youtube` → `ModeYouTube`, `never` → `SplitAndNever`. */
function goConst(prefix: string, v: string): string {
  return `trackparse.${prefix}${v === "youtube" ? "YouTube" : variant(v)}`;
}
/** A struct literal as gofmt writes it: one line for one field, else one aligned field per line. */
function goStruct(type: string, fields: [string, string][], indent: string): string {
  if (fields.length === 0) return `trackparse.${type}{}`;
  if (fields.length === 1) return `trackparse.${type}{${fields[0][0]}: ${fields[0][1]}}`;
  const w = Math.max(...fields.map(([k]) => k.length));
  return `trackparse.${type}{\n${fields.map(([k, v]) => `${indent}\t${`${k}:`.padEnd(w + 2)}${v},`).join("\n")}\n${indent}}`;
}
/**
 * A Go program around `body` (lines, one tab of indentation added). The imports are in two
 * groups, as goimports writes them: the standard library, a blank line, then the module.
 */
function goMain(body: string[], std: string[]): string {
  const mod = '"github.com/4matic/trackparse/go"';
  const groups = [[...new Set(std)].sort(), [mod]].filter((g) => g.length);
  const imports =
    std.length === 0 ? `import ${mod}` : `import (\n${groups.map((g) => g.map((x) => `\t${x}`).join("\n")).join("\n\n")}\n)`;
  return `package main\n\n${imports}\n\nfunc main() {\n${body.map((l) => (l ? `\t${l}` : "")).join("\n")}\n}`;
}

// ---- C ------------------------------------------------------------------------------------------
// Laid out as clang-format writes it with c/.clang-format (LLVM style, 100 columns, short ifs on
// one line). Checked by running clang-format over generated programs: they come out unchanged.

/** A C octal escape for each UTF-8 byte of `cp`: three digits, so a following digit is safe. */
function cOctal(cp: number): string {
  return [...new TextEncoder().encode(String.fromCodePoint(cp))].map((b) => `\\${b.toString(8).padStart(3, "0")}`).join("");
}

/**
 * The inside of a C string literal (no quotes). Control, invisible and bidi characters are octal
 * escapes of their UTF-8 bytes; a lone surrogate is U+FFFD, as the library reads invalid UTF-8;
 * a `?` after a `?` is escaped, so no trigraph forms. Other text is UTF-8 as written.
 */
function cEsc(s: string): string {
  let out = "";
  let prev = "";
  for (const ch of s) {
    const cp = ch.codePointAt(0) ?? 0;
    if (ch === '"' || ch === "\\") out += `\\${ch}`;
    else if (ch === "\n") out += "\\n";
    else if (ch === "\r") out += "\\r";
    else if (ch === "\t") out += "\\t";
    else if (ch === "?" && prev === "?") out += "\\?";
    else if (cp >= 0xd800 && cp <= 0xdfff) out += cOctal(0xfffd);
    else if (invisible(cp)) out += cOctal(cp);
    else out += ch;
    prev = ch;
  }
  return out;
}
/** A C string literal. */
export const cStr = (s: string) => `"${cEsc(s)}"`;

/** A double argument: `45`, `7.5`, `1e+21`; `INFINITY` and `NAN` need <math.h>. */
function cNum(n: number): string {
  if (!Number.isFinite(n)) return Number.isNaN(n) ? "NAN" : n > 0 ? "INFINITY" : "-INFINITY";
  return String(n);
}
/** The enum constant of an option value: `youtube` → `TRACKPARSE_MODE_YOUTUBE`. */
const cConst = (prefix: string, v: string) => `TRACKPARSE_${prefix}_${v.toUpperCase()}`;

/**
 * Rows of `args` starting at column `col`, and the number of string splits: as many arguments
 * per row as fit in 100 columns, a comma after each but the last, `tail` after the last. When an
 * argument cannot fit, clang-format gives each argument a row and splits the string literals that
 * are too long. Null when that does not fit either.
 */
function cPack(col: number, args: string[], tail: string): { rows: string[]; splits: number } | null {
  const rows: string[] = [];
  const end = (i: number) => (i === args.length - 1 ? tail : ",");
  for (let i = 0; i < args.length; i++) {
    const last = rows.length - 1;
    if (last >= 0 && col + cols(`${rows[last]} ${args[i]}${end(i)}`) <= WIDTH) rows[last] += ` ${args[i]}${end(i)}`;
    else if (col + cols(`${args[i]}${end(i)}`) <= WIDTH) rows.push(`${args[i]}${end(i)}`);
    else return cPackSplit(col, args, tail);
  }
  return { rows, splits: 0 };
}
function cPackSplit(col: number, args: string[], tail: string): { rows: string[]; splits: number } | null {
  const rows: string[] = [];
  let splits = 0;
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    const e = i === args.length - 1 ? tail : ",";
    if (col + cols(a + e) <= WIDTH) rows.push(a + e);
    else if (a.length > 1 && a.startsWith('"') && a.endsWith('"')) {
      const pieces = cSplit(a.slice(1, -1), col, e).map((p) => `"${p}"`);
      if (col + cols(pieces[pieces.length - 1] + e) > WIDTH || pieces.some((p) => col + cols(p) > WIDTH)) return null;
      splits += pieces.length - 1;
      pieces[pieces.length - 1] += e;
      rows.push(...pieces);
    } else return null;
  }
  return { rows, splits };
}

/**
 * `lhs fn(args);` at `indent`, as clang-format breaks it: flat when it fits; else the layout with
 * the lowest penalty among the arguments aligned after the parenthesis, a break after `=`, a break
 * after the parenthesis (arguments 4 columns in), and both. Penalties as clang-format's LLVM style
 * weighs them: 2 for the `=`, 19 for the parenthesis, about 18 for each break between arguments and
 * 1000 for each split of a string literal.
 */
function cCall(indent: string, lhs: string, fn: string, args: string[]): string {
  const flat = `${lhs}${fn}(${args.join(", ")});`;
  if (cols(indent + flat) <= WIDTH) return flat;
  // Continuation rows are padded relative to `indent`, which the caller adds to every row.
  const at = cols(indent);
  const pad = (col: number) => `\n${" ".repeat(col - at)}`;
  const options: { cost: number; text: string }[] = [];
  const add = (cost: number, head: string, col: number) => {
    const p = cPack(col, args, ");");
    if (!p) return;
    const breaks = p.splits ? args.length - 1 : p.rows.length - 1;
    options.push({ cost: cost + 18 * breaks + 1000 * p.splits, text: head + p.rows.join(pad(col)) });
  };
  const open = `${lhs}${fn}(`;
  // Aligned after the parenthesis, on the line of the call.
  add(0, open, at + cols(open));
  if (lhs) {
    // A break after `=`: the call on its own line, aligned after its parenthesis or broken after it.
    add(2, `${lhs.trimEnd()}${pad(at + 4)}${fn}(`, at + 4 + cols(`${fn}(`));
    add(21, `${lhs.trimEnd()}${pad(at + 4)}${fn}(${pad(at + 8)}`, at + 8);
  }
  add(19, `${open}${pad(at + 4)}`, at + 4);
  if (!options.length) return flat;
  return options.reduce((a, b) => (b.cost < a.cost ? b : a)).text;
}

/**
 * The pieces of one string literal at column `col`, split after a space where a piece would pass
 * 100 columns (clang-format's string breaking); `tail` follows the last piece.
 */
function cSplit(text: string, col: number, tail: string): string[] {
  const out: string[] = [];
  let rest = text;
  while (col + cols(`"${rest}"${tail}`) > WIDTH) {
    let cut = -1;
    let w = col + 1;
    const chars = [...rest];
    let i = 0;
    for (let k = 0; k < chars.length; k++) {
      w += cols(chars[k]);
      i += chars[k].length;
      if (w + 1 > WIDTH) break;
      if (chars[k] === " " && k < chars.length - 1) cut = i;
    }
    if (cut <= 0) break;
    out.push(rest.slice(0, cut));
    rest = rest.slice(cut);
  }
  out.push(rest);
  return out;
}

/**
 * `lhs "…";` for a string declaration: one literal per line of `text` (a break after each `\n`,
 * as clang-format keeps them), aligned after `=`, or on the next line when that saves a split.
 */
function cText(indent: string, lhs: string, text: string): string {
  const lines = text
    .split(/(?<=\n)/)
    .filter((x, i, all) => x !== "" || all.length === 1)
    .map(cEsc);
  const layout = (col: number) => lines.flatMap((l, i) => cSplit(l, col, i === lines.length - 1 ? ";" : ""));
  const same = layout(cols(indent + lhs));
  const next = layout(cols(indent) + 4);
  // Continuation rows are padded relative to `indent`, which the caller adds to every row.
  if (lines.length === 1 && same.length > 1 && next.length < same.length) {
    const pad = "\n    ";
    return `${lhs.trimEnd()}${pad}${next.map((p) => `"${p}"`).join(pad)};`;
  }
  const pad = `\n${" ".repeat(cols(lhs))}`;
  return `${lhs}${same.map((p) => `"${p}"`).join(pad)};`;
}

/** A C program: the includes, then `fns` (each a block of lines), then `main` around `body`. */
function cMain(body: string[], std: string[], fns: string[] = []): string {
  const inc = [...new Set(std)].sort().map((h) => `#include <${h}>`);
  const out = [...inc, "#include <trackparse.h>", ""];
  for (const f of fns) out.push(f, "");
  out.push("int main(void) {", ...body.map((l) => (l ? `  ${l}` : "")), "}");
  return out.join("\n");
}

/**
 * The string variable `name` holding `s`, and the length argument for it. Text with U+0000 is a
 * char array passed with its byte length, so the NUL stays in.
 */
function cInput(name: string, s: string): { decl: string; len: string } {
  if (s.includes("\0")) return { decl: cText("  ", `const char ${name}[] = `, s), len: `sizeof ${name} - 1` };
  return { decl: cText("  ", `const char *${name} = `, s), len: "TRACKPARSE_NUL_TERMINATED" };
}

/** `parse` and `format` as a C program. */
function cTrackCode(s: string, o: TrackOptions, f: FormatChoice): Snippet {
  const { decl, len } = cInput("input", s);
  const body = [decl];
  const api: string[] = [];
  const ind = "  ";
  const set: string[] = [];
  if (o.mode) set.push(cCall(ind, "", "trackparse_parse_options_set_mode", ["options", cConst("MODE", o.mode)]));
  if (o.uploader) set.push(cCall(ind, "", "trackparse_parse_options_set_uploader", ["options", cStr(o.uploader), "TRACKPARSE_NUL_TERMINATED"]));
  for (const a of o.knownArtists ?? [])
    set.push(cCall(ind, "", "trackparse_parse_options_add_known_artist", ["options", cStr(a), "TRACKPARSE_NUL_TERMINATED"]));
  if (o.splitAnd) set.push(cCall(ind, "", "trackparse_parse_options_set_split_and", ["options", cConst("SPLIT_AND", o.splitAnd)]));
  if (set.length) {
    api.push("trackparse_parse_with", "trackparse_parse_options");
    body.push(
      "trackparse_parse_options *options = trackparse_parse_options_new();",
      ...set,
      cCall(ind, "trackparse_track *track = ", "trackparse_parse_with", ["input", len, "options"]),
      "trackparse_parse_options_free(options);",
    );
  } else {
    api.push("trackparse_parse");
    body.push(cCall(ind, "trackparse_track *track = ", "trackparse_parse", ["input", len]));
  }
  body.push("if (!track) return 1; /* out of memory */", "");
  const fset: string[] = [];
  if (f.feat) fset.push(`trackparse_format_options_set_feat(format, ${cConst("FEAT", f.feat)});`);
  if (f.joiners) fset.push(`trackparse_format_options_set_joiners(format, ${cConst("JOINERS", f.joiners)});`);
  if (f.versions) fset.push(`trackparse_format_options_set_versions(format, ${cConst("VERSIONS", f.versions)});`);
  if (fset.length) {
    api.push("trackparse_format_with", "trackparse_format_options");
    body.push(
      "trackparse_format_options *format = trackparse_format_options_new();",
      ...fset,
      "trackparse_string *formatted = trackparse_format_with(track, format);",
      "trackparse_format_options_free(format);",
    );
  } else {
    api.push("trackparse_format");
    body.push("trackparse_string *formatted = trackparse_format(track);");
  }
  // printf stops at U+0000; fwrite writes the whole string.
  if (s.includes("\0"))
    body.push(
      "if (formatted) {",
      "  fwrite(formatted->str.ptr, 1, formatted->str.len, stdout);",
      "  putchar('\\n');",
      "}",
    );
  else body.push('if (formatted) printf("%s\\n", formatted->str.ptr);');
  body.push(
    "trackparse_string_free(formatted);",
    "trackparse_track_free(track);",
    "return 0;",
  );
  return { src: cMain(body.flatMap((l) => l.split("\n")), ["stdio.h"]), api };
}

/** `parseTracklist` and `applyEdits` as a C program. */
function cListCode(x: ListInput): Snippet {
  const { text, audioSeconds, knownArtists, seconds, from, fix } = x;
  const { decl, len } = cInput("text", text);
  const hasOptions = audioSeconds !== undefined || knownArtists !== undefined;
  const ind = "  ";
  const body = [decl, ""];
  if (hasOptions) {
    body.push("trackparse_tracklist_options *options = trackparse_tracklist_options_new();");
    // audio_seconds unset is "unknown"; set to 0 it is an audio of length 0.
    if (audioSeconds !== undefined) body.push(cCall(ind, "", "trackparse_tracklist_options_set_audio_seconds", ["options", cNum(audioSeconds)]));
    for (const a of knownArtists ?? [])
      body.push(cCall(ind, "", "trackparse_tracklist_options_add_known_artist", ["options", cStr(a), "TRACKPARSE_NUL_TERMINATED"]));
    body.push(cCall(ind, "trackparse_tracklist *list = ", "trackparse_parse_tracklist_with", ["text", len, "options"]), "trackparse_tracklist_options_free(options);");
  } else {
    body.push(cCall(ind, "trackparse_tracklist *list = ", "trackparse_parse_tracklist", ["text", len]));
  }
  body.push(
    "if (!list) return 1; /* out of memory */",
    "/* The entries (one per line), the warnings (with line numbers) and the stats, as JSON. */",
    "trackparse_string *json = trackparse_tracklist_to_json(list);",
    'if (json) printf("%s\\n\\n", json->str.ptr);',
    "trackparse_string_free(json);",
    "trackparse_tracklist_free(list);",
    "",
    "trackparse_edits *shift = trackparse_edits_new();",
    cCall(ind, "", "trackparse_edits_shift_from", ["shift", cNum(seconds), String(from)]),
    cCall(ind, "", "print_edited", ["text", len, "shift"]),
    "trackparse_edits_free(shift);",
    "",
    "trackparse_edits *sort = trackparse_edits_new();",
    "trackparse_edits_sort_by_time(sort);",
    cCall(ind, "", "print_edited", ["text", len, "sort"]),
    "trackparse_edits_free(sort);",
  );
  if (fix)
    body.push(
      "",
      "/* The suggested fix of the first warning that has one. */",
      "trackparse_edits *fix = trackparse_edits_new();",
      cCall(ind, "", "trackparse_edits_set_time", ["fix", String(fix.line), cNum(fix.seconds)]),
      cCall(ind, "", "print_edited", ["text", len, "fix"]),
      "trackparse_edits_free(fix);",
    );
  body.push("return 0;");
  const helper = [
    "/* Apply the edits to text and print the new text. */",
    "static void print_edited(const char *text, size_t len, const trackparse_edits *edits) {",
    "  trackparse_edit_result *result = trackparse_apply_edits(text, len, edits);",
    ...(text.includes("\0")
      ? ["  if (result) {", "    fwrite(result->text.ptr, 1, result->text.len, stdout);", '    printf("\\n\\n");', "  }"]
      : ['  if (result) printf("%s\\n\\n", result->text.ptr);']),
    "  trackparse_edit_result_free(result);",
    "}",
  ].join("\n");
  const std = ["stdio.h"];
  if ([audioSeconds ?? 0, seconds, fix?.seconds ?? 0].some((n) => !Number.isFinite(n))) std.push("math.h");
  const api = [
    hasOptions ? "trackparse_parse_tracklist_with" : "trackparse_parse_tracklist",
    ...(hasOptions ? ["trackparse_tracklist_options"] : []),
    "trackparse_tracklist_to_json",
    "trackparse_apply_edits",
    "trackparse_edits",
  ];
  return { src: cMain(body.flatMap((l) => l.split("\n")), std, [helper]), api };
}

// ---- one track ----------------------------------------------------------------------------------

/** `parse` and `format` with the playground's options, in one language. */
export function trackCode(lang: Lang, s: string, o: TrackOptions, f: FormatChoice): Snippet {
  if (lang === "c") return cTrackCode(s, o, f);
  if (lang === "js") {
    const po: { [k: string]: Value } = {};
    if (o.mode) po.mode = o.mode;
    if (o.splitAnd) po.splitAnd = o.splitAnd;
    if (o.knownArtists) po.knownArtists = o.knownArtists;
    if (o.uploader) po.uploader = o.uploader;
    const fo = { ...f } as { [k: string]: Value };
    const src = [
      'import { format, parse } from "trackparse";',
      "",
      jsCall("const track = ", "parse", Object.keys(po).length ? [s, po] : [s]),
      jsCall("", "format", Object.keys(fo).length ? [ident("track"), fo] : [ident("track")]),
    ].join("\n");
    return { src, api: ["parse", "format"] };
  }
  if (lang === "py") {
    const args: [string | null, Value][] = [[null, s]];
    if (o.mode) args.push(["mode", o.mode]);
    if (o.splitAnd) args.push(["split_and", o.splitAnd]);
    if (o.knownArtists) args.push(["known_artists", o.knownArtists]);
    if (o.uploader) args.push(["uploader", o.uploader]);
    const fargs = Object.entries(f).map(([k, v]) => `, ${k}=${strLit(String(v))}`).join("");
    const src = ["from trackparse import format_track, parse", "", pyCall("track = ", "parse", args), `format_track(track${fargs})`].join("\n");
    return { src, api: ["parse", "format_track"] };
  }
  if (lang === "rs") {
    // A whole program: options are builders, and the `_with` functions take them by reference.
    const names: string[] = [];
    const popts: ((indent: string) => string)[] = [];
    if (o.mode) popts.push(() => `.mode(ModeOption::${variant(o.mode ?? "")})`);
    if (o.knownArtists) {
      const items = o.knownArtists.map(rsStr);
      popts.push((indent) => rsArrayCall(".known_artists", items, indent));
    }
    if (o.uploader) popts.push(() => `.uploader(${rsStr(o.uploader ?? "")})`);
    if (o.splitAnd) popts.push(() => `.split_and(SplitAnd::${variant(o.splitAnd ?? "")})`);
    if (o.mode) names.push("ModeOption");
    if (o.splitAnd) names.push("SplitAnd");
    const fopts: ((indent: string) => string)[] = [];
    if (f.feat) fopts.push(() => `.feat(FeatPlacement::${variant(f.feat ?? "")})`);
    if (f.joiners) fopts.push(() => `.joiners(JoinerStyle::${variant(f.joiners ?? "")})`);
    if (f.versions) fopts.push(() => `.versions(VersionsOption::${variant(f.versions ?? "")})`);
    if (f.feat) names.push("FeatPlacement");
    if (f.joiners) names.push("JoinerStyle");
    if (f.versions) names.push("VersionsOption");
    const body: string[] = [];
    const api: string[] = [];
    if (popts.length) {
      names.push("parse_with", "ParseOptions");
      api.push("parse_with", "ParseOptions");
      body.push(rsChain("let options = ", "ParseOptions::new()", popts), rsCall("let track = ", "parse_with", [rsStr(s), "&options"]));
    } else {
      names.push("parse");
      api.push("parse");
      body.push(rsCall("let track = ", "parse", [rsStr(s)]));
    }
    if (fopts.length) {
      names.push("format_track_with", "FormatOptions");
      api.push("format_track_with", "FormatOptions");
      body.push(rsChain("let format = ", "FormatOptions::new()", fopts), 'println!("{}", format_track_with(&track, &format));');
    } else {
      names.push("format_track");
      api.push("format_track");
      body.push('println!("{}", format_track(&track));');
    }
    // The option enums after the functions and their option types.
    for (const n of ["ModeOption", "SplitAnd", "FeatPlacement", "JoinerStyle", "VersionsOption"]) if (names.includes(n)) api.push(n);
    const src = `${rsUse("trackparse", names)}\n\nfn main() {\n${body.map((l) => `    ${l}`).join("\n")}\n}`;
    return { src, api };
  }
  // Go: a whole program. Options are structs whose zero value means the default.
  const popts: [string, string][] = [];
  if (o.mode) popts.push(["Mode", goConst("Mode", o.mode)]);
  if (o.uploader) popts.push(["Uploader", goStr(o.uploader)]);
  if (o.knownArtists) popts.push(["KnownArtists", `[]string{${o.knownArtists.map(goStr).join(", ")}}`]);
  if (o.splitAnd) popts.push(["SplitAnd", goConst("SplitAnd", o.splitAnd)]);
  const fopts: [string, string][] = [];
  if (f.feat) fopts.push(["Feat", goConst("Feat", f.feat)]);
  if (f.joiners) fopts.push(["Joiners", goConst("Joiners", f.joiners)]);
  if (f.versions) fopts.push(["Versions", goConst("Versions", f.versions)]);
  const body = [
    popts.length
      ? `track := trackparse.ParseWith(${goStr(s)}, ${goStruct("ParseOptions", popts, "\t")})`
      : `track := trackparse.Parse(${goStr(s)})`,
    fopts.length
      ? `fmt.Println(trackparse.FormatWith(track, ${goStruct("FormatOptions", fopts, "\t")}))`
      : "fmt.Println(trackparse.Format(track))",
  ];
  const api = [...(popts.length ? ["ParseWith", "ParseOptions"] : ["Parse"]), ...(fopts.length ? ["FormatWith", "FormatOptions"] : ["Format"])];
  return { src: goMain(body, ['"fmt"']), api };
}

// ---- tracklist ----------------------------------------------------------------------------------

/** `parseTracklist` and `applyEdits` for the playground's text, options and edit inputs. */
export function listCode(lang: Lang, x: ListInput): Snippet {
  if (lang === "c") return cListCode(x);
  const { text, audioSeconds, knownArtists, seconds, from, fix } = x;
  const lines = text.split("\n");
  // Lists without CR line endings are written one line per row; anything else as one string.
  const joined = !(text.includes("\r") || lines.length < 2);
  const hasOptions = audioSeconds !== undefined || knownArtists !== undefined;
  if (lang === "js") {
    const opts: { [k: string]: Value } = {};
    if (audioSeconds !== undefined) opts.audioSeconds = audioSeconds;
    if (knownArtists) opts.knownArtists = knownArtists;
    const head = "const text = ";
    const textLine = joined ? `${head}${jsLay(lines, "", head, '.join("\\n");')}.join("\\n");` : `${head}${strLit(text)};`;
    const out = [
      'import { applyEdits, parseTracklist } from "trackparse/tracklist";',
      "",
      textLine,
      "",
      jsCall("const list = ", "parseTracklist", hasOptions ? [ident("text"), opts] : [ident("text")]),
      "list.entries; // one entry per line",
      "list.warnings; // the checks, with line numbers",
      "",
      `applyEdits(text, [{ op: "shift", seconds: ${numLit(seconds)}, from: ${from} }]);`,
      'applyEdits(text, [{ op: "sort-by-time" }]);',
    ];
    if (fix) out.push(`applyEdits(text, [{ op: "set-time", line: ${fix.line}, seconds: ${numLit(fix.seconds)} }]); // the suggested fix`);
    return { src: out.join("\n"), api: ["parseTracklist", "applyEdits"] };
  }
  if (lang === "py") {
    const flatText = `text = "\\n".join(${pyFlat(lines)})`;
    const textLines = !joined
      ? [`text = ${strLit(text)}`]
      : cols(flatText) <= WIDTH
        ? [flatText]
        : [`lines = ${pyLay(lines, "", "lines = ", "")}`, 'text = "\\n".join(lines)'];
    const args: [string | null, Value][] = [[null, ident("text")]];
    if (audioSeconds !== undefined) args.push(["audio_seconds", audioSeconds]);
    if (knownArtists) args.push(["known_artists", knownArtists]);
    const out = [
      "from trackparse.tracklist import apply_edits, parse_tracklist",
      "",
      ...textLines,
      "",
      pyCall("tl = ", "parse_tracklist", args),
      "tl.entries  # one entry per line",
      "tl.warnings  # the checks, with line numbers",
      "",
      `apply_edits(text, [{"op": "shift", "seconds": ${numLit(seconds)}, "from": ${from}}])`,
      'apply_edits(text, [{"op": "sort-by-time"}])',
    ];
    if (fix) out.push(`apply_edits(text, [{"op": "set-time", "line": ${fix.line}, "seconds": ${numLit(fix.seconds)}}])  # the suggested fix`);
    return { src: out.join("\n"), api: ["parse_tracklist", "apply_edits"] };
  }
  if (lang === "rs") {
    // A whole program. A list without CR line endings is an array of lines joined into a String.
    const t = joined ? "&text" : "text";
    const parseFn = hasOptions ? "parse_tracklist_with" : "parse_tracklist";
    const names = ["apply_edits", "Edit", parseFn];
    if (hasOptions) names.push("TracklistOptions");
    const rsOpts: ((indent: string) => string)[] = [];
    if (audioSeconds !== undefined) rsOpts.push(() => `.audio_seconds(${rsNum(audioSeconds)})`);
    if (knownArtists) {
      const items = knownArtists.map(rsStr);
      rsOpts.push((indent) => rsArrayCall(".known_artists", items, indent));
    }
    // rustfmt keeps an array on one line when it is at most 60 columns wide.
    const items = lines.map(rsStr);
    const array = `[${items.join(", ")}]`;
    const body = [
      !joined
        ? `let text = ${rsStr(text)};`
        : cols(array) <= 60
          ? `let text = ${array}.join("\\n");`
          : `let text = [\n${items.map((l) => `        ${l},`).join("\n")}\n    ]\n    .join("\\n");`,
      "",
      hasOptions
        ? `${rsChain("let options = ", "TracklistOptions::new()", rsOpts)}\n    let list = parse_tracklist_with(${t}, &options);`
        : `let list = parse_tracklist(${t});`,
      'println!("{:#?}", list.entries); // one entry per line',
      'println!("{:#?}", list.warnings); // the checks, with line numbers',
      "",
      `let shifted = apply_edits(${t}, &[Edit::shift_from(${rsNum(seconds)}, ${from})]);`,
      `let sorted = apply_edits(${t}, &[Edit::sort_by_time()]);`,
    ];
    if (fix) body.push(`let fixed = apply_edits(${t}, &[Edit::set_time(${fix.line}, ${rsNum(fix.seconds)})]); // the suggested fix`);
    body.push(`println!("{}\\n\\n{}${fix ? "\\n\\n{}" : ""}", shifted.text, sorted.text${fix ? ", fixed.text" : ""});`);
    const src = `${rsUse("trackparse::tracklist", names)}\n\nfn main() {\n${body.map((l) => (l ? `    ${l}` : "")).join("\n")}\n}`;
    return { src, api: [parseFn, ...(hasOptions ? ["TracklistOptions"] : []), "apply_edits", "Edit"] };
  }
  // Go: a whole program. A list without CR line endings is a slice of lines joined with "\n".
  const opts: [string, string][] = [];
  // AudioSeconds is a *float64: nil is unknown, a pointer to 0 an audio of length 0.
  if (audioSeconds !== undefined) opts.push(["AudioSeconds", `trackparse.Ptr(${goFloat(audioSeconds)})`]);
  if (knownArtists) opts.push(["KnownArtists", `[]string{${knownArtists.map(goStr).join(", ")}}`]);
  const nums = [audioSeconds ?? 0, seconds, fix?.seconds ?? 0];
  const std = ['"encoding/json"', '"fmt"', '"os"'];
  if (joined) std.push('"strings"');
  if (nums.some((n) => !Number.isFinite(n))) std.push('"math"');
  const body = [
    joined ? `text := strings.Join([]string{\n${lines.map((l) => `\t${goStr(l)},`).join("\n")}\n}, "\\n")` : `text := ${goStr(text)}`,
    "",
    hasOptions ? `list := trackparse.ParseTracklistWith(text, ${goStruct("TracklistOptions", opts, "")})` : "list := trackparse.ParseTracklist(text)",
    "enc := json.NewEncoder(os.Stdout)",
    'enc.SetIndent("", "  ")',
    "enc.SetEscapeHTML(false)  // & < > as written, as JSON.stringify does",
    "enc.Encode(list.Entries)  // one entry per line",
    "enc.Encode(list.Warnings) // the checks, with line numbers",
    "",
    `shifted := trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.ShiftFrom(${goNum(seconds)}, ${from})})`,
    "sorted := trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.SortByTime()})",
  ];
  if (fix) body.push(`fixed := trackparse.ApplyEdits(text, []trackparse.Edit{trackparse.SetTime(${fix.line}, ${goNum(fix.seconds)})}) // the suggested fix`);
  body.push(`fmt.Print(shifted.Text, "\\n\\n", sorted.Text${fix ? ', "\\n\\n", fixed.Text' : ""}, "\\n")`);
  const api = [
    hasOptions ? "ParseTracklistWith" : "ParseTracklist",
    ...(hasOptions ? ["TracklistOptions"] : []),
    ...(audioSeconds !== undefined ? ["Ptr"] : []),
    "ApplyEdits",
    "ShiftFrom",
    "SortByTime",
    ...(fix ? ["SetTime"] : []),
  ];
  return { src: goMain(body.flatMap((l) => l.split("\n")), std), api };
}

// ---- API docs -----------------------------------------------------------------------------------

const DOCS_RS = "https://docs.rs/trackparse/latest/trackparse/";
const PKG_GO = "https://pkg.go.dev/github.com/4matic/trackparse/go";

/** Rust items by kind and module, as docs.rs names their pages. */
const RS_ITEM: Record<string, string> = {
  parse: "fn.parse.html",
  parse_with: "fn.parse_with.html",
  format_track: "fn.format_track.html",
  format_track_with: "fn.format_track_with.html",
  ParseOptions: "struct.ParseOptions.html",
  FormatOptions: "struct.FormatOptions.html",
  ModeOption: "enum.ModeOption.html",
  SplitAnd: "enum.SplitAnd.html",
  FeatPlacement: "enum.FeatPlacement.html",
  JoinerStyle: "enum.JoinerStyle.html",
  VersionsOption: "enum.VersionsOption.html",
  parse_tracklist: "tracklist/fn.parse_tracklist.html",
  parse_tracklist_with: "tracklist/fn.parse_tracklist_with.html",
  apply_edits: "tracklist/fn.apply_edits.html",
  TracklistOptions: "tracklist/struct.TracklistOptions.html",
  Edit: "tracklist/enum.Edit.html",
};

/** The site's reference sections, which are the API docs of the JavaScript and Python packages. */
const SITE_REF: Record<string, string> = {
  parse: "reference/parse/",
  format: "reference/functions/#formattrack-options",
  format_track: "reference/functions/#formattrack-options",
  parseTracklist: "reference/tracklist/#parsetracklisttext-options",
  parse_tracklist: "reference/tracklist/#parsetracklisttext-options",
  applyEdits: "reference/tracklist/#applyeditstext-edits-options",
  apply_edits: "reference/tracklist/#applyeditstext-edits-options",
};

/** The C names: the site's reference pages have a C tab for each of these functions. */
const C_REF: Record<string, string> = {
  trackparse_parse: SITE_REF.parse,
  trackparse_parse_with: SITE_REF.parse,
  trackparse_format: SITE_REF.format,
  trackparse_format_with: SITE_REF.format,
  trackparse_parse_tracklist: SITE_REF.parseTracklist,
  trackparse_parse_tracklist_with: SITE_REF.parseTracklist,
  trackparse_apply_edits: SITE_REF.applyEdits,
};

/** Where `name` is documented for `lang`: docs.rs, pkg.go.dev, or the site's reference. */
export function apiUrl(lang: Lang, name: string, base: string): string | null {
  if (lang === "rs") return RS_ITEM[name] ? DOCS_RS + RS_ITEM[name] : null;
  if (lang === "c") return C_REF[name] ? `${base}/${C_REF[name]}` : null;
  if (lang === "go") return `${PKG_GO}#${name}`;
  return SITE_REF[name] ? `${base}/${SITE_REF[name]}` : null;
}
