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
    - `clean` / `youtube`: position **only if** the remainder still contains a spaced dash separator
      (R6.1). (`3 - Artist - Title` → 3; `311 - Amber` → artist `311`.)
    - `filename`: **always** a position (`311 - Amber.mp3` → 311, `10 - Double Digit.wav` → 10).
  - **(d)** `N` not zero-padded followed by a space and a word → **never** a position in `clean` /
    `youtube` mode (`2 Unlimited`, `808 State`, `50 Cent`, `21 Savage`).
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
