// Helpers shared by the C code generators (gen_data.mjs, gen_unicode.mjs).

/** The UTF-8 bytes of a string (or of an array of code points). */
export function utf8(s) {
  const text = Array.isArray(s) ? String.fromCodePoint(...s) : s;
  return [...Buffer.from(text, "utf8")];
}

const isHex = (b) => (b >= 0x30 && b <= 0x39) || (b >= 0x41 && b <= 0x46) || (b >= 0x61 && b <= 0x66);

/**
 * A C string literal of the UTF-8 bytes of s, ASCII only: printable ASCII as is, everything else
 * as \xNN (MSVC reads sources in the system code page). A hex escape followed by a hex digit is
 * split into two literals ("\xc3" "a"), since C hex escapes take every hex digit that follows.
 * '?' is escaped so no trigraph can form (-std=c11 reads trigraphs).
 */
export function cStr(s) {
  const bytes = utf8(s);
  let out = '"';
  let afterHex = false;
  for (const b of bytes) {
    if (b === 0x22 || b === 0x5c || b === 0x3f) {
      out += `\\${String.fromCharCode(b)}`;
      afterHex = false;
    } else if (b >= 0x20 && b < 0x7f) {
      if (afterHex && isHex(b)) out += '" "';
      out += String.fromCharCode(b);
      afterHex = false;
    } else {
      out += `\\x${b.toString(16).padStart(2, "0")}`;
      afterHex = true;
    }
  }
  return `${out}"`;
}

/** A constant tp_str initializer: {"abc", 3}. */
export function cTpStr(s) {
  return `{${cStr(s)}, ${utf8(s).length}}`;
}

/** camelCase or kebab-case to snake_case. */
export function snake(s) {
  return s
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[-]/g, "_")
    .toLowerCase();
}

/** The linkage macro the generated headers declare their arrays with. */
export const PRIVATE_DATA = [
  "/* Linkage of generated tables. A declaration needs the complete type: the amalgamation makes",
  " * these static, and a static tentative declaration must have a complete type. In the",
  " * separate-files build they are extern, so the header declares and data_gen.c / unicode_gen.c",
  " * define. */",
  "#ifndef TP_PRIVATE_DATA",
  "#ifdef TRACKPARSE_AMALGAMATION",
  "#define TP_PRIVATE_DATA static",
  "#else",
  "#define TP_PRIVATE_DATA extern",
  "#endif",
  "#endif",
];

/** Write or check one generated file. Returns true when it is stale (check) or was written. */
export function emitFile(fs, path, content, check, label) {
  let current = "";
  try {
    current = fs.readFileSync(path, "utf8");
  } catch {}
  if (current === content) return false;
  if (check) {
    console.error(`${label} is stale: run node ${process.argv[1].replace(/.*\/c\//, "c/")}`);
  } else {
    fs.writeFileSync(path, content);
    console.log(`wrote ${path}`);
  }
  return true;
}
