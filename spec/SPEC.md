# trackparse specification

Spec version: see [`VERSION`](VERSION) (currently **0.1.0**).

This document is the contract every port implements. Behaviour is pinned by the fixtures in
[`fixtures/`](fixtures/); every rule below has an ID (`R6.2`) that fixtures cite in their `rules` field.
When this document and a fixture disagree, the fixture is a bug report against one of them — fix
both in the same change.

Word lists live **only** in [`data/`](data/). The rules refer to them by file name
(`data/junk.json`). Ports compile them into source; never copy lists by hand.

**Guiding principle — extract, don't delete.** Everything recognised in the input ends up in a
field (artists, versions, junk, flags, position…). Anything not recognised stays in the title
verbatim. A reader of the output can always account for every word of the input.

---

## Contents

- [Output model](#output-model)
- [R0 Portability](#r0-portability)
- [R1 Normalization](#r1-normalization)
- [R2 Mode prelude](#r2-mode-prelude)
- [R3 Group scanner and skeleton](#r3-group-scanner-and-skeleton)
- [R4 Prefixes: timestamp and position](#r4-prefixes-timestamp-and-position)
- [R5 Group classification](#r5-group-classification)
- [R6 Artist/title separator](#r6-artisttitle-separator)
- [R7 Artist side](#r7-artist-side)
- [R8 Title side](#r8-title-side)
- [R9 Assembly and dedup](#r9-assembly-and-dedup)
- [R10 parseArtists](#r10-parseartists)
- [R11 format](#r11-format)
- [R12 Options](#r12-options)
- [R13 Tracklists](#r13-tracklists)
- [Invariants](#invariants)
- [Glossary](#glossary)

---

## Output model

Schema: [`schema/parsed-track.schema.json`](schema/parsed-track.schema.json). Every key is always
present (use `null` / `[]` / `false`, never omit).

```jsonc
{
  "input": "01. Fox Stevenson ft. X & Y - Bruises (Magnetude Remix) [Official Video]",
  "mode": "youtube",
  "position": { "raw": "01", "number": 1 },
  "timestamp": null,
  "artists": [
    { "name": "Fox Stevenson", "role": "primary",  "joiner": null,  "source": "artist" },
    { "name": "X",             "role": "featured", "joiner": "ft.", "source": "artist" },
    { "name": "Y",             "role": "featured", "joiner": "&",   "source": "artist" }
  ],
  "title": "Bruises",
  "fullTitle": "Bruises (Magnetude Remix)",
  "versions": [
    { "type": "remix", "raw": "Magnetude Remix",
      "artists": [{ "name": "Magnetude", "role": "remixer", "joiner": null, "source": "version" }],
      "modifiers": [], "descriptor": null, "year": null, "unknownArtist": false, "delimiter": "(" }
  ],
  "year": null,
  "flags": { "explicit": false, "clean": false, "unknownArtist": false, "unknownTitle": false },
  "junk": [{ "raw": "Official Video", "kind": "video" }],
  "warnings": []
}
```

| Field | Meaning |
|---|---|
| `input` | The original string, untouched (not normalized). |
| `mode` | Resolved mode: `clean`, `youtube` or `filename` (R2). |
| `position` | Track number prefix (R4). `raw` is the digits as written (`"01"`), `number` the integer. |
| `timestamp` | Leading cue time (R4). `raw` as written without brackets, `seconds` integer. |
| `artists` | Track credits: `primary`, `featured`, `producer`. **Remixers are not here** — they live in `versions[].artists`. |
| `title` | The clean title: no feat, versions, junk, flags, year groups. Unrecognised groups stay (R8). |
| `fullTitle` | `title` + every version re-attached in order with its original delimiter (R9.4). |
| `versions` | Every recognised version/remix group or dash suffix, left to right (R5.7). |
| `year` | The first year in text order from a bare year group `(2015)`, a year-only dash suffix or a dropped year-only credit name (R7.5). Versions carry their own `year`. |
| `flags` | `explicit`, `clean` (R5.4); `unknownArtist`, `unknownTitle` (R7.5, R8.6). |
| `junk` | Removed noise with a `kind`, in text order (position in the normalized string, not removal order). |
| `warnings` | Stable codes flagging heuristics that fired (list in schema). Order of first emission, no duplicates. |

### Artist

| Key | Meaning |
|---|---|
| `name` | Name as written (after R1 normalization and R7.4 cleanup). Never empty. |
| `role` | `primary` \| `featured` \| `remixer` \| `producer`. `vs`, `x`, `&` do **not** create roles; they are recorded as the joiner. |
| `joiner` | Raw joiner or marker text that preceded the name in its credit list, trimmed, case kept (`"&"`, `","`, `"x"`, `"vs."`, `"ft."`, `"Feat."`, `"with"`). `null` for the first name of every credit list (the first primary, the first remixer of a version). The first featured artist's joiner is the feat marker (`"ft."`, `"feat."`, `"featuring"`, `"with"`, `"w/"`). The first producer's joiner is the producer marker as written (`"prod. by"`, `"Prod."`). "First" means first *remaining*: see joiner inheritance (R7.6). |
| `source` | Where the credit was found: `artist` (left of the separator), `title` (right of the separator, outside versions), `version` (inside a version). |

### Version

| Key | Meaning |
|---|---|
| `type` | See `data/version-keywords.json#types`. |
| `raw` | Group inner text (or dash-suffix text), trimmed, whitespace collapsed. |
| `artists` | Remixer credits (`role: "remixer"`, `source: "version"`). |
| `modifiers` | Canonical lowercase keys of modifier-run words that did not become the type, left to right, deduped (`"Extended VIP Mix"` → `["extended"]`). |
| `descriptor` | Leftover text that is neither credit, modifier, head nor year — genres (`"D'n'B"`), possessives (`"Taylor's"`), `"at Wembley"`. As written; multiple pieces joined by one space. `null` if none. |
| `year` | 1900–2099 found inside the version, else `null`. |
| `unknownArtist` | `true` when the credit is an unknown token (`"ID Remix"`). |
| `delimiter` | `"("`, `"["`, `"{"` for groups, `"-"` for dash suffixes. |

---

## R0 Portability

Rules that keep JS, Python and Rust byte-identical.

- **R0.1** No regex lookaround, backreferences or `\p{…}` classes. Recognition is done with
  hand-written scanners over code points. Small anchored regexes are allowed only for fixed shapes
  (digits, timestamps).
- **R0.2** *Whitespace* means exactly this code-point set: U+0009–U+000D, U+0020, U+0085, U+00A0,
  U+1680, U+2000–U+200A, U+2028, U+2029, U+202F, U+205F, U+3000. Never use a language's `\s`.
- **R0.3** *Keyword comparison* (feat markers, version words, junk, joiners, stopwords…) uses the
  **word key**: ASCII-lowercase the word (only A–Z → a–z) and strip **one** trailing character from
  `. , ! ? : ;` — except when the vocabulary entry itself ends with that character (`"feat."`,
  `"vs."`), in which case the word is first compared unstripped. Multi-word vocabulary entries match
  a sequence of consecutive words, longest entry first.
- **R0.4** *Dedup key* (R9 only): full Unicode lowercase → NFD → remove U+0300–U+036F → collapse
  whitespace → trim.
- **R0.5** Iterate by code point. No index/offset is exposed in the output.
- **R0.6** *Letter* means Unicode general category **L** (Lu, Ll, Lt, Lm, Lo): JS `/\p{L}/u`,
  Python `str.isalpha()` on one code point, Rust via a general-category table. Letter numbers (Nl,
  `Ⅰ`) and combining marks (Mn/Mc) are **not** letters. Only R6.7 uses this test. *Digit* means
  ASCII `0`–`9` only.
- **R0.7** A *word* is a maximal run of non-whitespace code points in a (sub)string.
- **R0.8** Parsing is total: any string input returns a ParsedTrack; never throw/panic. Runtime is
  linear-ish in input length (no backtracking blowups).

## R1 Normalization

Applied first by every public function. Exported as `normalize(input)`; idempotent.

- **R1.1** Unicode NFC.
- **R1.2** Remove U+200B, U+2060, U+FEFF, U+00AD, and U+E000 (reserved as the R3.4 group placeholder). U+200C (ZWNJ) and U+200D (ZWJ) are **kept**: ZWJ
  emoji sequences (`👨‍👩‍👧`) and Persian/Indic text need them.
- **R1.3** Map fullwidth/CJK brackets to ASCII: `（`→`(`, `）`→`)`, `［【〔`→`[`, `］】〕`→`]`, `｛`→`{`, `｝`→`}`.
- **R1.4** Map quotes: `“ ” „ « » 「 」 『 』 ＂` → `"`; `‘ ’ ‚ ` ´` → `'`.
- **R1.5** Map dashes: U+2010–U+2015, U+2212, U+FE58, U+FE63, U+FF0D → `-`. Then any run of 2+
  `-` surrounded by whitespace on both sides (`" -- "`) → single `-`.
- **R1.6** Map every whitespace code point (R0.2) to U+0020, collapse runs to one space, trim.
- **R1.7** `×` (U+00D7) is kept (it is a joiner).
- **R1.8** Re-apply NFC as the final step (removals can leave a decomposable sequence), so
  `normalize` stays idempotent.

All later rules operate on the normalized string. `input` in the output is the original.

## R2 Mode prelude

- **R2.1 Resolution.** `options.mode` (default `auto`). `auto` resolves, checked in order:
  1. `filename` — the string ends with `.` + an extension from `data/extensions.json` (ASCII
     case-insensitive).
  2. `youtube` — any of: a platform suffix matches (R2.2); a top-level ` | ` exists (outside groups);
     any top-level group classifies as junk (R5.1, excluding `genre`); the string ends with an
     unbracketed junk phrase (R8.4 test).
  3. `clean` otherwise.

  `auto` never resolves to `tracklist`; that mode is only used when asked for (R13.6).
- **R2.2 Platform suffixes (all modes).** While the string ends (ASCII case-insensitive) with an
  entry of `data/platform-suffixes.json` (longest first), remove it and push junk
  `{ raw: <suffix without its leading " - " / " | ">, kind: "platform" }`. Runs before anything else in R2–R6.
- **R2.3 filename.** Strip the final `.<ext>` when `<ext>` is in `data/extensions.json` (ASCII
  case-insensitive: `.MP3` counts), then trim (`01 - .mp3` → `01 -`). Then, if the remaining string
  contains **no space** and contains `_`, replace every `_` with a space.
  (`01_track_name.mp3` → `01 track name`) and trim again. Otherwise `_` is kept.
- **R2.4 youtube pipes.** Split the string at top-level ` | ` and ` || ` (outside groups).
  With one segment, nothing happens. Otherwise:
  1. Segments that contain **no spaced dash** and are entirely junk vocabulary (same test as R5.1,
     including the label-suffix rule, applied to the bare segment) are removed and pushed to `junk`
     with their kind. A segment with a spaced dash is never junk here. If every segment is junk, the
     working string is empty.
  2. Of the rest, keep the **first segment containing a spaced dash** (R6.1) as the working string;
     the other remaining segments are pushed to `junk` with kind `other`.
  3. If none contains a dash and exactly **2** remain: treat them as `artist | title` (left = artist
     side, right = title side), warn `ambiguousSeparator`, and skip R6.
  4. Otherwise keep the first remaining segment; push the others as junk `other`.

## R3 Group scanner and skeleton

- **R3.1** Scan left to right with a stack of expected closers for `(`→`)`, `[`→`]`, `{`→`}`.
  A *top-level group* is an opener at depth 0 up to its matching closer. Nested groups stay raw
  inside their parent's inner text.
- **R3.2** A closer that does not match the top of the stack is literal text.
- **R3.3** An opener still open at end of input: the group runs to the end of the string with
  `closed = false`; warn `unbalancedBrackets`. Its inner text is everything after the opener.
- **R3.4** The *skeleton* is the string with every top-level group replaced by one placeholder
  code point (U+E000). Separator, feat-marker and joiner searches run on skeletons, so text inside
  brackets can never be split by them. Reassembly substitutes groups back in order.
- **R3.5** Each group records: `open` char, `inner` text (trimmed, whitespace collapsed), `closed`.

## R4 Prefixes: timestamp and position

Applied to the working string after R2, before R6. At most one timestamp, then at most one position.

- **R4.1 Timestamp.** At the start of the string: optional `[` or `(`, then `H:MM:SS`,
  `HH:MM:SS`, `M:SS` or `MM:SS` (MM and SS are two digits 00–59; H/HH/M are 1–2 digits), then the
  matching `]`/`)` if one was opened, then either end of string or a space, optionally followed by
  `- ` (a spaced dash). `timestamp = { raw: <time without brackets>, seconds }`. Strip it.
- **R4.2 Position.** At the (new) start, let `N` be a run of 1–3 digits (4+ digits: never a position).
  The forms are tried in the order (a), (b), (c), (d)/(e); the first that applies decides. The
  *remainder* is the text left after `N` and the separator the form consumed.
  - **(a)** `N` followed by `.` or `)` then a space or end → position. (`01. Aphex Twin - Xtal`, `1) Intro`)
  - **(b)** `N` zero-padded (starts with `0`, length ≥ 2) followed by a space → position, in every
    mode. (`01 Aphex Twin - Xtal`, `01 - Artist - Title`, `01 A - T.mp3` → 1)
  - **(c)** `N` not zero-padded followed by ` - `:
    - `clean` / `youtube` / `tracklist`: position **only if** the remainder still contains a spaced dash separator
      (R6.1). (`3 - Artist - Title` → 3; `311 - Amber` → artist `311`.)
    - `filename`: **always** a position (`311 - Amber.mp3` → 311, `10 - Double Digit.wav` → 10).
  - **(d)** `N` not zero-padded followed by a space and a word → **never** a position in `clean` /
    `youtube` / `tracklist` mode (`2 Unlimited`, `808 State`, `50 Cent`, `21 Savage`).
  - `tracklist` mode (R13.6) behaves like `clean` throughout R4.2; bare numbers in a list are
    R13.5's job.
  - **(e) filename mode only:** `N` followed by `-`, `_` or `.` directly followed by a non-space →
    position (`01-Track`, `03_name`); `N` followed by a space and a word → position **only if** the
    remainder contains no spaced dash (`3 My Song.mp3` → 3; `2 Unlimited - No Limit.mp3` → not).
  - When a position is taken, strip it and any following `- ` (spaced dash) or single separator char
    consumed by the matched form. The remainder must be non-empty, otherwise it is not a position.
  - A `knownArtists` entry matching at the start (R7.3a; here it may also be followed by `-`, so
    `3 Doors Down-Kryptonite.mp3` is protected) blocks (a)–(e).
- **R4.3** No other rule ever strips leading digits from artist names.

## R5 Group classification

`classify(inner)` → one of `Junk | Feat | Producer | Flag | Year | Versions | Unknown`, checked in
this order. Applied to **every** top-level group on both sides, and (where noted) to dash suffixes.
*Words* are whitespace-separated; comparisons use word keys (R0.3).

- **R5.1 Junk.** The whole inner text tokenizes (greedy, longest first) into phrases from
  `data/junk.json#phrases`, `data/genres.json`, connector tokens from `data/junk.json#connectors`
  and year words (1900–2099, `Official Video 2015`), with at least one phrase. Kind = the kind of
  the first phrase (`genre` for genres); a year inside junk stays in the junk `raw` and does not
  set `year`. Additionally a group whose **last** word key is in `data/junk.json#labelSuffixes`
  and has ≥ 2 words (`[NCS Release]`, `[Monstercat Release]`, `(Hospital Records)`) → junk kind `label`.
  Junk `raw` = inner text.
- **R5.2 Feat.** The first word key is a feat marker (`data/feat-markers.json#markers` or
  `#bracketOnly`) and at least one more word follows. For `with`/`w/` the next word key must not be
  in `data/stopwords.json` (`(With You)` stays Unknown). The rest is split by R7.3 into artists with
  `role: featured`; the first gets the marker (as written) as joiner.
- **R5.3 Producer.** The first word key is in `data/feat-markers.json#producer`, optionally followed
  by the word `by`, and at least one more word follows. The rest is split by R7.3 into
  `role: producer`; the first gets the marker text as written (`"prod. by"`, `"Prod."`) as joiner.
  (`prod.by` as one word is accepted.)
- **R5.4 Flag.** Inner word keys equal `explicit`, `explicit version`, `dirty`, `dirty version` →
  `flags.explicit`; `clean`, `clean version`, `radio clean` → `flags.clean`. A lone `E` is **not** a flag.
- **R5.5 Year.** Exactly one word, 4 digits, 1900–2099 → sets `year` (if already set, the first wins).
- **R5.6 Multi-version.** If the inner text contains top-level ` / ` or `; ` and **every** part
  classifies as a Version (R5.7), the group yields several versions (same delimiter, `raw` per part).
- **R5.7 Version.** Checked in this order; the first that succeeds wins:
  1. **Head-by form.** First word key is a head (`data/version-keywords.json#heads`) and the second
     is `by`, with ≥ 1 more word: `Remix by Skrillex` → type from head, credits = the rest (R7.3).
  2. **Head scan.** The **last** word (or multi-word entry ending at the last word) is a head.
     Walk left from the head collecting a contiguous run of words that are: type-capable modifiers
     (`#typeCapableModifiers`), descriptors (`data/descriptors.json`), genre phrases (`data/genres.json`),
     years, or other heads. Everything left of that run is the **credit span**.
     - A head is *generic* when its canonical type is in `#genericHeads` (mix, edit, version),
       whatever its spelling (`ver`, `re-edit` count).
     - **Type**, first that applies:
       1. the head is generic and the word directly left of it is type-capable → that modifier's
          type (`Radio Edit`→radio, `Extended Mix`→extended, `VIP Mix`→vip, `Original Mix`→original,
          `Dub Mix`→dub, `Acoustic Version`→acoustic);
       2. the head is generic and the run contains a non-generic head → the type of the non-generic
          head nearest the generic head (`2011 Remastered Version`→remaster, `VIP Remix Edit`→remix);
       3. the head is `mix` and the credit span is non-empty → `remix` (`Levela Mix`);
       4. otherwise the head's canonical type (`rmx`→remix, `remastered`→remaster, `Edit`→edit,
          `Mix`→mix, `Version`→version).
     - **Modifiers:** canonical keys of type-capable/descriptor words in the run that did not supply
       the type, left to right, deduped (`Extended VIP Mix` → type vip, modifiers `["extended"]`).
       A run word that is both a head and type-capable (`vip`, `dub`) counts as a modifier when it
       does not supply the type (`VIP Remix` → remix, modifiers `["vip"]`). Other heads in the run
       are ignored.
     - **Year:** a year word in the run → `year`.
     - **Descriptor:** genre phrases in the run, as written.
     - **Credit span:**
       - empty → no artists.
       - an unknown token (R7.5, `ID`) → `unknownArtist: true`, no artists.
       - the type is `version` or `cover`, or the span consists entirely of junk phrases
         (`data/junk.json#phrases`) → the span goes to `descriptor` as written (before any genre
         descriptor), no artists (`Test Ver`, `Japanese Version`, `Taylor's Version`, `Adele Cover`,
         `a Mazzy Star cover`, `Video Edit`).
       - ends with possessive `'s` → strip the `'s` and credit it (`Skrillex's Remix` → Skrillex).
       - otherwise split with R7.3 → `role: remixer`, `source: version`.
     - **Warning:** if type became `remix` via generic `mix` + credit and the last credited name has
       ≥ 3 words, warn `ambiguousMixCredit` (`Fonetick Lee & Sonic Art Space Drums Mix`).
  3. **Prefix form.** The first word(s) match `#prefixForms` (`Live`, `Remastered`, `Acoustic`,
     `Sped Up`, `Slowed + Reverb`, `Nightcore`…). Type from the table; a year word anywhere after it →
     `year`; the remaining words (excluding the prefix and the year) → `descriptor`
     (`Live at Wembley 1986` → live, year 1986, descriptor `at Wembley`; `Remastered 2015` → remaster, 2015).
- **R5.8 Unknown.** Anything else. The group stays **in place, verbatim** (with its brackets) in the
  title or artist text: `(I Can't Get No) Satisfaction`, `Song (Part 2)`, `(Rotation Deep UK)`,
  `(Moving Shadow 2003)`.

## R6 Artist/title separator

Operates on the skeleton of the working string (after R2, R4).

- **R6.1 Spaced dashes.** A *spaced dash* is `-` with a space immediately on both sides; the start
  of the string counts as a space (so a leading `- Title` has an empty `s0`). Split the skeleton at
  every spaced dash → segments `s0 … sn`.
- **R6.2 Suffix peeling.** While `n ≥ 1`, test the last segment `sn`. It is **peelable** if
  `classify(sn)` (applied to the bare text; placeholders make it Unknown) is Junk, Flag, Year or
  Versions **and** at least one holds: (i) after peeling ≥ 2 segments remain; (ii) `sn` has ≥ 2
  words and is not a prefix-form version (R5.7.3); (iii) `sn` contains a year; (iv) it is Junk.
  Peel it (versions get `delimiter: "-"`, and are ordered by position in the string, i.e. before
  later peeled ones), then repeat.
  - `Hey Jude - Remastered 2015` → title `Hey Jude`, version remaster 2015, no artist.
  - `Strobe - Radio Edit` → title `Strobe`, version radio.
  - `The Beatles - Hey Jude - Remastered 2015` → artist/title + version.
  - `Band - Live` → artist `Band`, title `Live` (one word, two segments: not peeled).
  - `Andy C b2b Hedex - Live at Printworks` → not peeled (prefix form: (ii) does not apply).
  - R6.2 applies in every mode.
- **R6.3 First dash.** If ≥ 2 segments remain: artist side = `s0`, title side = `s1 … sn` joined
  with ` - ` (later dashes stay in the title). If `s0` is empty (string starts with `- `), there is no
  artist side.
- **R6.4 Asymmetric dash.** Else, the first `-` that has a space on exactly one side and a
  non-space on the other (`A -T`, `A- T`) → split there, warn `asymmetricDashSplit`.
- **R6.5 Quoted title (youtube only).** Else, a double-quoted span `"…"` that does not start at
  position 0 and is closed: title side = the quoted text; artist side = text before it with a
  trailing `:` or `-` and spaces trimmed; text after the closing quote is appended to the title side
  (it will usually be groups/junk). Warn `quotedTitleSplit`.
- **R6.6 ` by ` (youtube only).** Else, the **last** occurrence of the lowercase word `by` with
  spaces on both sides, where the right side is non-empty and is not a single stopword
  (`data/stopwords.json`) → title side = left, artist side = right. Warn `bySplit`. Placeholders
  are ignored in both tests (`Stand by Me`, `Stand by Me (Official Audio)` → no split).
- **R6.7 Unspaced dash (youtube and filename only).** Else, candidates are `-` with a non-space on
  both sides whose right neighbour is a letter or `"`. If a `knownArtists` entry equals the text left
  of a candidate, use that candidate; otherwise use the **last** candidate. Warn `unspacedDashSplit`.
  (`Blink-182-All The Small Things` → `Blink-182` / `All The Small Things` because `-182` is rejected;
  `Jay-Z-Numb` → `Jay-Z` / `Numb`.) In `clean` mode unspaced dashes never split (`Track-Name`).
- **R6.8 No separator.** Else: no artist side; the whole string is the title side; warn
  `noSeparator` (not for empty input). In youtube mode with `options.uploader`, the artist side is
  the uploader with a trailing ` - Topic` and a trailing `VEVO` (case-sensitive, optionally preceded
  by a space) removed, and `noSeparator` is **not** emitted.
- **R6.9** `:` is never a separator in v1 (except inside R6.5).
- **R6.10 Tracklist mode.** With `mode: "tracklist"` (R13.6), R6.2 does not peel the segment that
  follows the **first** spaced dash: if a spaced dash exists, the artist side is `s0` and the title
  side is `s1 … sn` (R8.2 still peels later dash suffixes on the title side). Without a spaced dash,
  in this order:
  - R6.4 applies;
  - else the first `_-_` (underscore, dash, underscore) is the separator (warn `unspacedDashSplit`);
  - else, only when the caller enables it (R13.4.2 list context), the **last** `-` with a letter
    (R0.6) on its left and an uppercase letter on its right is the separator (warn
    `unspacedDashSplit`). As in R6.7, the last candidate wins: `Jay-Z-Numb` → `Jay-Z` / `Numb`.
    *Uppercase* is Unicode general category Lu (JS `/\p{Lu}/u`, Python
    `unicodedata.category(c) == "Lu"`).

  R6.5–R6.7 (youtube/filename rules) do not apply.

*Notes (intended):* a string whose only dash was peeled as a suffix and that has no artist still
warns `noSeparator` (`Hey Jude - Remastered 2015`).

## R7 Artist side

- **R7.1 Groups.** Classify each top-level group of the artist side. Feat → featured credits
  (`source: artist`), Producer → producer credits, Year → `year`, Junk → `junk`, Flag → `flags`
  (`Eminem (Explicit) - Stan`). These groups are removed. Versions and Unknown groups on the artist
  side stay verbatim in the text.
  Extracted groups' credits are appended **after** the credits of the text before them.
- **R7.2 Unbracketed feat.** On the artist skeleton, find the **leftmost** word whose word key is a
  feat marker (`#markers`, not `#bracketOnly`) with a space (or placeholder) on the left and a space
  on the right. Left of it → main credit list (all `role: primary`). Right of it → featured list
  (all `role: featured`, first joiner = the marker as written). Later feat markers in the featured
  list act as joiners. Names containing marker letters (`Featurecast`, `Ftampa`) never match.
- **R7.3 Credit-list splitting** (used for every credit list: main, featured, producer, remixer).
  Scan left to right, leftmost match wins, at each name start:
  - **(a) knownArtists.** If a `knownArtists` entry (compared with R0.4 dedup keys) matches here and
    is followed by end of list, whitespace, `,`, `;` or `-`, take it as one name (longest entry wins).
  - **(b) Joiners.** Otherwise look for the leftmost joiner occurrence:
    - *spaced* joiners (`data/joiners.json#spaced`): the word(s) with a space on both sides, compared
      by word key; entries with `caseSensitive: true` (`x`) must match exactly (so ` X ` does **not**
      split — `Malcolm X & Y`);
    - *tight* joiners (`,`, `;`): the character with optional spaces around, **except** a `,`
      between two digits (`1,000`).
  - **Guards:**
    - an unspaced `/` or `&` or `+` never splits (`AC/DC`, `Drum&Bass`, `Ant+Shift`);
    - do not split at `&`, `and`, `+` when the next word key is in `data/no-split-before.json`
      (`Mumford & Sons`). The next word ends at whitespace or at a tight joiner
      (`Mumford & Sons,will.i.am` → `Mumford & Sons`, `will.i.am`);
    - `and` (joiner kind `and`) splits per `options.splitAnd`: `always`; `never`; `auto` (default) =
      only if this credit list already split at a tight joiner (`,` or `;`) before it
      (`A, B and C` → 3; `A; B and C` → 3; `Simon and Garfunkel` → 1);
    - `with` splits only in the main list of the artist side (R7.2 left side), never inside groups.
  - Each resulting name gets `joiner` = the joiner text as written (trimmed), first name `null`
    (or the marker for featured/producer lists, per R5.2/R5.3/R7.2).
- **R7.4 Name cleanup.** Before splitting, trim the whole credit list and strip one pair of
  wrapping `"` or `'` from it — the list starts and ends with the same quote character and contains
  no other occurrence of it (`"Camo & Krooked" - X` → Camo, Krooked). Then per name: trim; strip
  one pair of wrapping `"` or `'`; strip trailing `,` `;`; strip a leading `by ` (word key).
  Interior dots and symbols are kept (`Mr. Oizo`, `will.i.am`, `A$AP Rocky`, `P!nk`,
  `Johny J.P.R.`). Empty names are dropped.
- **R7.5 Drops and unknowns.**
  - A name that is only a year (1900–2099) is dropped **unless** it is the only name in the list.
    The dropped year is a candidate for the track `year` (R9.6); inside a version it sets
    `version.year` if that is still `null`.
  - A name that is an unknown token (`data/unknown-tokens.json`) is dropped and sets
    `flags.unknownArtist` (or `version.unknownArtist` inside a version). This holds for every
    credit list, including featured and producer lists (`(feat. ID)` → `flags.unknownArtist`).
- **R7.6 Joiner inheritance.** When the first remaining name of a credit list is removed (R7.4
  empty, R7.5 drop, R9.2 dedup), the next remaining name of that list takes its joiner. So the
  first remaining primary or remixer always has `null`, and the first remaining featured or
  producer name carries the marker (`ID & Noisia` → Noisia `null`;
  `Sub Focus - X (feat. Sub Focus & Alice Gold)` → Alice Gold `feat.`). A removed name that is not
  first just loses its joiner.

## R8 Title side

- **R8.1 Groups.** Classify each top-level group. Versions → `versions` (with the group's bracket
  as `delimiter`); Junk → `junk`; Flag → `flags`; Year → `year`; Feat → featured credits
  (`source: title`); Producer → producer credits (`source: title`); Unknown → stays in the title.
- **R8.2 Dash suffixes.** Split the title skeleton at spaced dashes and peel from the right with the
  R6.2 test **without** condition (i)'s segment requirement (an artist side exists, so a single-word
  version like `Title - VIP` is peeled when a separator was found in R6.3; if R6.8 found no separator,
  use the full R6.2 test).
- **R8.3 Unbracketed feat / prod.** On the remaining title skeleton, find markers with spaces
  around, left to right:
  - *feat markers:* only `data/feat-markers.json#titleMarkers` (`feat.`, `ft.`, `featuring`;
    compared per R0.3, so `Feat.`, `FT.` count) plus any `options.keywords.featMarkers`.
    Undotted `feat` / `ft` stay text (`Rhapsody - A Feat of Strength`).
  - *producer markers:* only `#titleProducer` (`prod.`, `prod. by`, `produced by`, `prod.by`).
    Undotted `prod` counts only inside brackets (R5.3).

  Everything from after a marker up to the next placeholder, the next marker of the other kind, or
  the end is a credit list (`source: title`): featured for feat markers, producer for producer
  markers (`a lot ft. J. Cole prod. DJ Dahi` → J. Cole featured, DJ Dahi producer). Inside a
  featured list, later feat markers act as joiners (R7.2). Markers and lists are removed from the
  title.
- **R8.4 Trailing junk (youtube only).** Repeatedly remove a trailing unbracketed run of words that
  is exactly a junk phrase (`data/junk.json#phrases`, not genres), optionally preceded by a spaced
  dash or `|`. Push as junk. When several phrases end at the last word, take the longest
  (`Official Video` beats `Video`). Never empty the title: stop when the phrase would remove the
  last remaining word(s).
- **R8.5 Cleanup.** Remove extracted groups, collapse whitespace, trim; strip one pair of wrapping
  `"` or `'` around the entire title; strip leading/trailing lone `-` or `|` left behind.
- **R8.6 Unknown title.** If the final title is an unknown token (`ID`, `???`, `Untitled`…), keep it
  as the title and set `flags.unknownTitle`.

## R9 Assembly and dedup

- **R9.1 Artist order.** Artist-side credits (text order, R7.1 appended groups), then title-side
  credits in text order (R8.1 groups and R8.3 unbracketed, by position).
- **R9.2 Dedup.** Using R0.4 keys over `artists`, in two separate classes: primary/featured
  credits, and producer credits. Within a class keep the first occurrence; the classes never
  interact, so a producer is never removed or role-upgraded because the same name is also primary
  or featured (`Noisia x Mefjus - X (prod. by Noisia)` keeps Noisia as primary **and** as
  producer). Between primary and featured the precedence is `primary` > `featured`: a later
  featured (or equal-role) duplicate is removed; on a later primary duplicate (*role upgrade*) the
  kept entry takes that occurrence's `role`, `joiner` and `source`, keeps its position and name,
  and the later occurrence is removed. Joiners then follow R7.6: a removed occurrence that was the
  first remaining name of its list passes its joiner to the next remaining name of that list —
  except on a role upgrade, where the kept entry takes over that joiner itself; and a kept entry
  that leaves its original list on a role upgrade passes its old joiner on in that list the same
  way. Remixers are deduped only within their own version and are never removed because they also
  appear as primary (self-remixes are real).
- **R9.3 Versions order.** Left to right by position in the string (artist-side versions stay text).
- **R9.4 fullTitle.** `title` followed, for each version, by a space and the version rendered with
  its delimiter: `(raw)`, `[raw]`, `{raw}`, or `- raw` for `"-"`. No feat, junk, flags or year.
  If `title` is empty, no leading space.
- **R9.5 Empty input.** `""` (after R1) → mode resolved normally (`clean` for auto), everything
  empty/null/false, `title` and `fullTitle` `""`, no warnings.
- **R9.6 Track year.** `year` is the first year in **text order** (position in the normalized
  string) among: Year groups (R5.5, R7.1, R8.1), year-only dash suffixes (R6.2, R8.2) and dropped
  year-only credit names (R7.5). Processing order does not matter
  (`Moby - Porcelain (1999) - 2015` → 1999).
- **R9.7 Junk order.** `junk` is ordered by position in the normalized string (text order), not by
  removal order; platform suffixes (R2.2) sit at the end where they were written.

*Notes (intended):* `fullTitle` appends versions **after** the Unknown groups kept in `title`, so
`Nonsense (Warm Roller Remix) (Sinuous)` has `fullTitle` `Nonsense (Sinuous) (Warm Roller Remix)`.

## R10 parseArtists

`parseArtists(input, options)` → `Artist[]`. Applies R1, R3 and R7 to the whole string as an
artist side (no R2 mode logic, no R4, no R6), then R9.2 dedup. `source` is `artist` for all.
Title-like groups (versions, unknown) stay part of names verbatim.

*Notes (intended):* no R4 prefix stripping, so `parseArtists("01. Gydra")` → `01. Gydra`.

## R11 format

`format(track, options)` → string. Defaults: `feat: "source"`, `joiners: "original"`,
`versions: "all"`, `producers: true`, `position: false`.

- **R11.1 Artist string.** Primary credits joined by their joiners: spaced joiners render as
  `" " + joiner + " "`, tight joiners as the raw character + `" "` (`", "`, `"; "`). Joiner `null`
  on a non-first name renders as `&`.
- **R11.2 Featured.** `feat: "source"` — featured with `source: artist` follow the primaries as
  `" " + <first joiner> + " " + names…` (their own joiners between them); featured with
  `source: title` go into the title as ` (<first joiner> names…)` after `title`. `"artist"` puts all
  featured in the artist string, `"title"` all into the title group, `"omit"` drops them.
  `featMarker` overrides the first featured joiner.
- **R11.3 Title.** `title`, then title-featured group, then versions (R9.4 rendering) if
  `versions: "all"`, then ` (<producer marker> names)` when `producers` and producers exist.
- **R11.4 Result.** `artists + " - " + title-part` when there are any artist-string credits,
  else the title part. `position: true` prefixes `"<raw>. "`.
- **R11.5 canonical joiners** map raw joiners through `data/joiners.json` `canonical` (so `;` → `,`,
  rendered `", "`); feat markers become `featCanonical` (`feat.`). `with` between primaries stays
  `with`; it becomes `feat.` only as the marker that starts a featured list.
- **R11.6 Round-trip.** `parse(format(parse(s)))` equals `parse(s)` on `artists` (name, role),
  `title`, and `versions` (type, artists' names) whenever `parse(s)` produced no warnings.

## R12 Options

Schema: [`schema/options.schema.json`](schema/options.schema.json).

| Option | Default | Effect |
|---|---|---|
| `mode` | `auto` | R2. |
| `uploader` | – | R6.8 youtube fallback artist. |
| `knownArtists` | `[]` | R7.3a/R4.2/R6.7: names never split. There is **no built-in list**. |
| `splitAnd` | `auto` | R7.3 guard for `and`. |
| `keywords` | – | Additive vocabulary: `versionHeads` (key → type), `descriptors`, `genres`, `junk` (phrase → kind), `featMarkers`. Keys are lowercased with ASCII rules. |

---

## R13 Tracklists

`parseTracklist(text, options)` reads a block of text line by line; `applyEdits(text, edits,
options)` rewrites it. Both live in a separate entry (`trackparse/tracklist`, Python
`trackparse.tracklist`). Schema: [`schema/tracklist.schema.json`](schema/tracklist.schema.json).
The single-line rules R1–R12 are unchanged; tracklist behaviour is only reachable through this
section and `mode: "tracklist"`.

### R13.1 Lines and the lossless guarantee

- **R13.1.1** Lines end at LF, CRLF or CR (only these: U+2028/U+2029, U+0085 and the other R0.2
  characters are **not** line breaks). Each entry keeps its terminator in `eol`: `"\n"`, `"\r\n"`,
  `"\r"`, or `""` for a last line without one. A trailing empty segment after the final terminator is
  not an entry (`"a\n"` has one entry). `""` has no entries.
- **R13.1.2 Lossless.** For every input: concatenating `raw + eol` over all entries gives the input
  exactly. `raw` is never normalized (R1 runs only inside the per-line `parse`).
- **R13.1.3 Parts.** Each entry has `parts = { lead, time, gap, rest }` (strings, no offsets, R0.5)
  with `lead + time + gap + rest === raw`. Without a time (R13.2), `lead`, `time` and `gap` are `""`
  and `rest === raw`.

### R13.2 Times

Read at the start of `raw` (this scanner is separate from R4.1, which is unchanged):

- **R13.2.1 lead:** R0.2 whitespace, then an optional `[` or `(`.
- **R13.2.2 time:** `A:B` or `A:B:C` in ASCII digits, then an optional fraction (`.` or `,` and 1–3
  digits). With three parts: `A` is 1–2 digits (hours), `B` and `C` are 2 digits each and at most 59.
  With two parts: `A` is 1–3 digits (minutes, **any value**, so `125:30` is 7530 s), `B` is 2 digits and
  at most 59. With 4+ fraction digits there is no fraction: the `.`/`,` is a gap separator (R13.2.3).
  - `seconds`: let `W` = hours·3600 + minutes·60 + seconds (an integer). Without a fraction,
    `seconds = W`; with one, `seconds` is the double nearest the decimal string `"<W>.<fraction
    digits>"` (correctly rounded: JS `Number()`, Python `float()`), so ports agree bit for bit
    (`07.500` → 7.5).
- **R13.2.3 gap:** the matching `]` / `)` if one was opened (else no time), then R0.2 whitespace, then
  optionally one separator from `-` `–` `—` `|` `.` `:` and whitespace, then optionally an arrow
  `=>` / `==>` and whitespace. The time must be followed by end of line, whitespace, a separator or
  the closing bracket; otherwise there is no time.
- **R13.2.4 invalid times:** the time candidate (after lead) is the **maximal** run
  `digits(:digits)+`, plus an optional fraction. When that run fails R13.2.2 (`1:75`, `0:61:00`,
  `1:02:75`, `1:02:03:04`), it is not a time: the entry has no time, `rest === raw`, and the
  tracklist warns `invalid-time` for the line.
  - There is no fallback to a shorter shape (`1:02:75` is not read as `1:02`).
  - A well-formed time followed by a character R13.2.3 does not allow (`12:34x`), or an opened
    bracket that is not closed after the time, means there is no time and **no** warning.
- **R13.2.5** `time` in the output is `{ raw, seconds }` (`raw` without brackets), or `null`.

### R13.3 Line kinds

`kind` is decided in this order (word keys per R0.3):

1. `blank`: `raw` is empty or only R0.2 whitespace.
2. Lines **with** a time: `marker` when the track part (R13.4) has no separator and any of its words is
   in [`data/marker-words.json`](data/marker-words.json) (plus `options.markerWords`); else `track`.
   A line that is only a time (its track part is empty after R1) is a `track` and warns
   `line-unparsed` (R13.7).
3. Lines **without** a time: `link` when `rest` (trimmed) starts with `http://`, `https://` or `www.`
   (ASCII case-insensitive); `header` when it (trimmed of R0.2 whitespace) ends with `:` or its
   first word is in
   [`data/header-words.json`](data/header-words.json) and it has at most one more word; `marker` as in
   2; `track` when the track part has a separator; else `note`.

Only `track` and `marker` entries have a `track` (a ParsedTrack); the others have `track: null`.
Nothing is ever dropped.

### R13.4 The track part

- **R13.4.1** The track part is `rest`, minus a bare position (R13.5). It is parsed with `parse(rest,
  { mode, knownArtists, keywords, splitAnd })`, `mode` defaulting to `"tracklist"`.
- **R13.4.2** *Candidate lines* are non-blank lines with a time or a spaced dash. List context for
  R6.10's tight dash is enabled when at least half of the candidate lines contain a spaced dash.
  "Contains a spaced dash" is tested on the skeleton R6 searches for the track part, i.e. after
  R1, R2 and R4 (R6.1), not on `raw`: the ` - ` in a gap (`00:00 - Intro`) or one consumed by a
  position (`01 - Title`) does not count.

### R13.5 Bare positions

A track part that starts with 1–3 ASCII digits and a space, where R4.2 does not take a position, has a
**bare position** candidate `N`. It is a position when the nearest preceding or the nearest following
candidate line (a line with a time or a spaced dash, R13.4.2) also has a bare candidate equal to
`N - 1` or `N + 1`. The digits and the space are
removed before parsing and `track.position = { raw, number }`. Dotted positions (`01.`) never count
as neighbours, so `2 Unlimited - No Limit` between `01.` and `03.` lines stays an artist.

### R13.6 Tracklist mode

`mode: "tracklist"` is a single-line mode (R6.10) accepted by `parse()` too; `parseTracklist` uses it
by default and adds the list context (R13.4.2, R13.5). R2's youtube and filename steps do not run.

### R13.7 Warnings

`warnings` is a list of `{ code, line, severity, suggestion }` sorted by line, then by the order of
the codes in the table below (`suggestion` is `null` unless stated). Per-line ParsedTrack warnings stay in `entries[].track.warnings` and are not
repeated.

| Code | Severity | When |
|---|---|---|
| `time-missing` | warning | a `track` entry has no time while at least one other entry has one |
| `time-out-of-order` | warning | a timed entry's seconds are below the previous timed entry's |
| `time-duplicate` | warning | equal to the previous timed entry's seconds |
| `no-start-at-zero` | info | the first timed entry is not at 0 |
| `time-after-audio-end` | warning | seconds above `options.audioSeconds` |
| `invalid-time` | warning | R13.2.4 |
| `position-gap` | warning | a positioned track entry's number is not the previous positioned entry's number + 1 (or equal) |
| `position-duplicate` | warning | equal to the previous positioned entry's number |
| `line-unparsed` | warning | a `track` entry whose track part is empty after R1, or whose ParsedTrack warns `noSeparator` |

**Suggestion for `time-out-of-order`.** A *search* of a raw time between bounds `lo` and `hi` (each
optional) makes the strings that change 1 digit, then 2 digits, of the raw time (fraction digits
excluded) that are valid under R13.2.2 (fields after the first at most 59) and satisfy `lo < t` and
`t < hi` (`t` per R13.2.2). Only the smallest number of changed digits with any candidate counts.

1. Search the out-of-order entry's time between the previous timed entry's seconds and the next
   timed entry's (if any).
2. Only when step 1 finds **no** candidate at all: search the **previous** timed entry's time
   between the timed entry before it (if any) and the out-of-order entry's seconds.

When the step that ran last finds exactly **one** candidate, it becomes `suggestion: { line, raw,
seconds }`, where `line` is the line to change (the warning's own line in step 1, the previous timed
line in step 2); several candidates or none give `null`. The warning itself always stays on the
out-of-order line. (`00:06:29` between `01:04:39` and `01:08:10` → line itself, `01:06:29`;
`01:22:21` then `01:03:55` between `01:00:08` and `01:06:20` → previous line, `01:02:21`.)

### R13.8 Output

`{ specVersion, entries, warnings, stats }`. Each entry is `{ line, kind, eol, raw, parts, time,
track }`. `stats` = `{ lines, tracks, markers, timed, firstTime, lastTime }`: entry count, `track`
count, `marker` count, entries with a time, the first and the last timed entry's seconds in line
order (`null` when none). Every key is always present.

### R13.9 Edits

`applyEdits(text, edits, options)` → `{ text, changed, warnings }`. Edits apply in order, each to the
result of the previous one; line numbers are 1-based in the text the edit sees.

| Edit | Effect |
|---|---|
| `{ op: "set-time", line, seconds }` | rewrite that line's `parts.time` |
| `{ op: "shift", seconds, lines?, from? }` | add `seconds` to the timed lines listed, or to every timed line from `from` on, or to all timed lines |
| `{ op: "sort-by-time" }` | reorder blocks by time (below) |
| `{ op: "set-rest", line, rest }` | replace `parts.rest` |
| `{ op: "insert", after, text }` | insert a line after line `after` (`0` = at the top) |
| `{ op: "delete", line }` | remove a line |

- **R13.9.1 Locality.** Lines not named by an edit keep `raw` and `eol` byte for byte. `changed` lists
  the line numbers, in the final text, of lines an edit wrote (sorted, unique; deleted lines do
  not count). `applyEdits(t, [])` returns `t`, `[]`, `[]`.
  - A line counts when any edit rewrote its `raw` or its `eol`, even if the result equals the
    original (a shift round trip lists its lines; `set-time` to the same value counts).
  - Lines that only move up or down because of an insert or delete do not count. Lines that
    `sort-by-time` moves to another slot count.
- **R13.9.2 Time style.** A rewritten time keeps its line's style: hours present or not, the hour
  and minute digit widths (as minimum widths), the fraction separator and digit count (values are
  rounded half up to that precision: units = `floor(v · 10^d + 0.5)` for `d` fraction digits). A value
  of an hour or more on a line without hours is written `HH:MM:SS`, or `H:MM:SS` when all the text's
  times with hours (at least one) use one-digit hours, and the edit warns `style-upgraded`.
  - *Long-minute style:* a two-part time whose minutes are already 60 or more on that line stays
    two-part when it grows (`125:30` + 600 → `135:30`, no warning). Its minimum minute width is
    min(original width, 2), so a short result never keeps 3 minute digits (`125:30` − 7000 →
    `08:50`).
  - A value that cannot be written as a valid R13.2.2 time is `edit-invalid` (R13.9.6): 100 hours
    or more, more than 999 minutes in long-minute style, or `v · 10^d` outside the safe-integer
    range (±(2^53 − 1)).
- **R13.9.3 Clamp.** A time that would go below 0 becomes 0 and the edit warns `time-clamped`.
- **R13.9.4 Sort.** A block is a timed line plus the untimed lines after it; lines before the first
  timed line stay first. Blocks are sorted by seconds, stable for equal values. `eol` values stay in
  their slots (slot *i* keeps the input's *i*-th `eol`), so a text without a trailing newline keeps it.
  A blank line that lands in the last slot (`eol` `""`) is no line on re-split (R13.1.1), so the
  line count can drop by one; this is allowed. The lossless guarantee (R13.1.2) is about parsing,
  not about the line count after edits.
- **R13.9.5 Insert and delete.** Inserted text must not contain a line break. The new line takes the
  text's most common non-empty `eol` (ties: the one seen first; `"\n"` if there is none); inserting
  after a last line whose `eol` is `""` gives that line the new `eol` and the inserted line `""`.
  Inserting into an empty text gives the line `eol` `""` (`""` + insert `x` → `"x"`): a text without
  a trailing newline keeps that property. Deleting the last line moves its `eol` to the new last
  line (that line counts as changed when its `eol` differs).
  - After every edit the lines are re-read as R13.1.1 would split their concatenation: a line with
    `eol` `"\r"` followed by an empty line with `eol` `"\n"` merges into one line with `"\r\n"`
    (that line counts as changed), and an empty last line without `eol` disappears.
- **R13.9.6 Totality.** An edit with a missing line, a line without a time (`set-time`), a missing or
  non-finite number (`null` counts; fixtures write infinity as `1e999`), a time that cannot be
  written (R13.9.2), a line break in `rest`/`text`, or an unknown `op` changes nothing and warns
  `edit-invalid`. Never throws. Edit warnings are `{ code, edit, line }` (`edit` = index in `edits`).
  - `edit-invalid`'s `line` is the edit's `line` when it is an integer, else its `after` when that is
    an integer, else `null`. `time-clamped` / `style-upgraded` carry the rewritten line.
  - `shift`: `lines` wins over `from`; listed lines without a time are skipped; every listed line
    and `from` must be an existing line, otherwise the edit is invalid. Without either, all timed
    lines shift.
- **R13.9.7 Round trip.** `shift(+s)` then `shift(-s)` on the same lines returns the original text
  when neither edit warned (`time-clamped`, `style-upgraded`, `edit-invalid`) **and** no rewritten
  time changed its digit count (widths are minimum widths: `0:00` + 600 → `10:00`, then − 600 →
  `00:00`). The same holds only for shifts that are a multiple of each line's fraction precision
  (otherwise rounding is lossy).

### R13.10 Options

`parseTracklist` and `applyEdits` take `mode` (default `"tracklist"`), `knownArtists`, `keywords`,
`splitAnd` (R12), `audioSeconds` (number, enables `time-after-audio-end`) and `markerWords` (extra
marker words). Bad values are ignored (R0.8).

### R13.11 Tracklist invariants

1. Lossless (R13.1.2) and `lead + time + gap + rest === raw` for every string.
2. Totality: any string and any edit list return without throwing; 10 000 lines finish well under
   one second.
3. Locality and identity (R13.9.1).
4. Round trip (R13.9.7).
5. A one-line text's track equals `parse(rest, { mode: "tracklist" })` for a line without a bare
   position.

---

## Invariants

Property tests in every port assert:

1. **Totality.** `parse` never throws/panics for any Unicode string; a 10 000-code-point input
   completes well under 100 ms.
2. **Normalize idempotent.** `normalize(normalize(s)) == normalize(s)`.
3. **Noise-insensitive.** Inserting the invisible chars R1.2 removes, doubling spaces, or swapping
   ` - `/` – `/` — ` does not change `parse` output except `input`.
4. **Schema.** Every output validates against `parsed-track.schema.json`.
5. **Artist hygiene.** Names are non-empty, trimmed, never equal to a joiner/marker; no two
   non-producer entries in `artists` share a dedup key, and no two producer entries do.
6. **Conservation.** Every word of `normalize(input)` — minus separators, joiners, feat/producer
   markers and bracket characters — appears in some output string field (title, artist names,
   version raw, junk raw, position/timestamp raw, uploader not counted).
7. **Metamorphic** (on generated inputs without warnings):
   - prefixing `01. ` only sets `position`;
   - appending ` (Official Video)` only adds a junk entry (and may change `mode` to youtube);
   - `(X Remix)` ↔ `[X Remix]` changes only `delimiter`;
   - `REMIX` ↔ `remix` changes only `raw`.
8. **Round-trip** (R11.6).

## Glossary

- **Working string** — the normalized input after R2 stripping.
- **Skeleton** — working string with top-level groups replaced by placeholders (R3.4).
- **Credit list** — a substring that R7.3 splits into names.
- **Word key** — R0.3.
- **Dedup key** — R0.4.
