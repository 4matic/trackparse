#!/usr/bin/env node
// dump_numbers.mjs: number cases from Node for c/tests/test_num.c (src/num.c).
//
//   node c/scripts/dump_numbers.mjs [--n 1000000] [--seed 1] [--out file.jsonl]
//
// Writes JSONL, one case per line, half of each kind:
//   {"s":"<decimal string>","v":"<String(Number(s))>","b":"<hex bits of Number(s)>"}
//   {"d":"<hex bits of a double>","s":"<String(d)>"}
// The strings follow the grammar tp_parse_number accepts (see src/num.c). The mix: random bit
// patterns, subnormals, powers of ten and their neighbours, integers, exact halfway points between
// neighbouring doubles (and one unit above and below in a far digit), long digit strings, short
// decimals like the R13.2 times, and exponent forms.
// The committed sample c/tests/data/numbers_sample.jsonl is --n 2000 --seed 1.

import { createWriteStream } from "node:fs";

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const N = Number(opt("n", "1000000"));
const out = opt("out", null);
let seed = BigInt(opt("seed", "1")) || 1n;

// xorshift64*, deterministic across runs.
const M64 = (1n << 64n) - 1n;
function next64() {
  seed ^= seed >> 12n;
  seed ^= (seed << 25n) & M64;
  seed ^= seed >> 27n;
  return (seed * 2685821657736338717n) & M64;
}
const rand = (n) => Number(next64() % BigInt(n));

const buf = new DataView(new ArrayBuffer(8));
const toBits = (d) => {
  buf.setFloat64(0, d);
  return buf.getBigUint64(0);
};
const fromBits = (b) => {
  buf.setBigUint64(0, b);
  return buf.getFloat64(0);
};
const hex = (b) => b.toString(16).padStart(16, "0");

const digits = (n, lead = true) => {
  let s = lead ? String(1 + rand(9)) : String(rand(10));
  for (let i = 1; i < n; i++) s += String(rand(10));
  return s;
};

// Exact decimal of m * 2^e (m, e BigInt), as an integer digit string and a power-of-ten exponent.
function exactDecimal(m, e) {
  if (e >= 0n) return { d: (m << e).toString(), p: 0 };
  const k = -e;
  return { d: (m * 5n ** k).toString(), p: -Number(k) };
}
const sci = ({ d, p }) => {
  // d * 10^p as a plain or exponent string, chosen at random.
  const t = d.replace(/^0+/, "") || "0";
  if (rand(2) === 0) return `${t}e${p}`;
  if (p >= 0) return t + "0".repeat(p);
  const k = -p;
  if (k >= t.length) return `0.${"0".repeat(k - t.length)}${t}`;
  return `${t.slice(0, t.length - k)}.${t.slice(t.length - k)}`;
};

function randomDouble() {
  switch (rand(8)) {
    case 0:
    case 1:
      return fromBits(next64());
    case 2:
      return fromBits(next64() & ((1n << 52n) - 1n)); // subnormal
    case 3:
      return 10 ** (rand(650) - 325) * (rand(2) ? 1 : -1);
    case 4:
      return fromBits(toBits(10 ** (rand(620) - 310)) + BigInt(rand(5)) - 2n);
    case 5:
      return rand(2 ** 30) * 2 ** (rand(40) - 20);
    case 6:
      return Number(digits(1 + rand(17))) / 10 ** rand(25);
    default: {
      const pick = [0, -0, 5e-324, 1e21, 1e-7, 1e-6, 1e20, 9007199254740992, 1.7976931348623157e308,
        2.2250738585072014e-308, 123456789012345680000, 0.1, 1 / 3, NaN, Infinity, -Infinity];
      return pick[rand(pick.length)];
    }
  }
}

function randomString() {
  const sign = ["", "", "", "-", "+"][rand(5)];
  switch (rand(9)) {
    case 0: {
      // Halfway point between a finite double and its successor (up to 767 significant digits).
      let b = next64() & ((1n << 63n) - 1n);
      if (rand(3) === 0) b &= (1n << 54n) - 1n; // subnormals and the smallest normals
      if (b >= 0x7fefffffffffffffn) b = 0x7fe0000000000000n;
      const be = b >> 52n;
      const f = b & ((1n << 52n) - 1n);
      const m = be === 0n ? f : f | (1n << 52n);
      const e = be === 0n ? -1074n : be - 1075n;
      const ex = exactDecimal(2n * m + 1n, e - 1n);
      const tweak = rand(3);
      if (tweak !== 0) {
        // One unit far below the last digit, up or down: no longer a tie.
        const extra = 3 + rand(20);
        let d = BigInt(ex.d) * 10n ** BigInt(extra);
        d += tweak === 1 ? 1n : -1n;
        return sign + sci({ d: d.toString(), p: ex.p - extra });
      }
      return sign + sci(ex);
    }
    case 1:
      return sign + digits(1 + rand(4)) + "." + digits(1 + rand(25), false); // times, R13.2
    case 2:
      return sign + digits(1 + rand(40)) + "." + digits(rand(40), false);
    case 3:
      return sign + "0." + "0".repeat(rand(330)) + digits(1 + rand(30));
    case 4:
      return sign + digits(1 + rand(20)) + ["e", "E"][rand(2)] + ["", "+", "-"][rand(3)] +
        String(rand(400)).padStart(1 + rand(3), "0");
    case 5:
      return sign + digits(100 + rand(1200)) + (rand(2) ? `e-${rand(1400)}` : "");
    case 6: {
      // A double's shortest digits with noise appended.
      const d = Math.abs(randomDouble());
      if (!Number.isFinite(d) || d === 0) return `${sign}0.0`;
      let s = String(d);
      if (!s.includes("e") && !s.includes(".")) s += ".";
      const [mant, ex] = s.split("e");
      return sign + mant + digits(rand(30), false) + (ex === undefined ? "" : `e${ex}`);
    }
    case 7: {
      const forms = ["0", "00", "007", "1.", ".5", "0.0", "1e21", "1e-7", "1e308", "1e309",
        "2e-324", "3e-324", "1e-400", "4.9406564584124654e-324", "2.4703282292062327e-324",
        "2.4703282292062328e-324", "1.7976931348623158e308", "1.7976931348623159e308",
        "9007199254740993", "3167.12345678901234567890", ".0e5", "0e999999999999",
        "1e-999999999999", "123456789012345678901234567890e-30"];
      return sign + forms[rand(forms.length)];
    }
    default:
      return sign + digits(1 + rand(25)) + "e" + String(rand(700) - 350);
  }
}

const stream = out ? createWriteStream(out) : process.stdout;
let pending = [];
const flush = () => {
  if (pending.length) stream.write(`${pending.join("\n")}\n`);
  pending = [];
};
for (let i = 0; i < N; i++) {
  if (i % 2 === 0) {
    const s = randomString();
    const v = Number(s);
    pending.push(JSON.stringify({ s, v: String(v), b: hex(toBits(v)) }));
  } else {
    const d = randomDouble();
    pending.push(JSON.stringify({ d: hex(toBits(d)), s: String(d) }));
  }
  if (pending.length >= 10000) flush();
}
flush();
if (out) stream.end();
