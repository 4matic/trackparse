# Proposal: tracklists (multi-line parsing, checks and edits)

> **Status: adopted as SPEC R13 (with R6.10) in 0.3.0.** [`SPEC.md`](../SPEC.md) is the contract
> now; this file is kept as the record of why. The rule numbers below are the draft ones and do not
> match R13's.
>
> **Corrections after verification.** Checking this draft against the corpus and the shipped
> implementation turned up these errors. The text below is left as written.
>
> - Bare positions: the corpus has **81** lines with a bare number (the whole "Gydra 10 years mix"),
>   not the 3 examples in 3a. All 81 are found (R13.5).
> - The 23 `noSeparator` lines in 3a: only **17** were really parsed wrong. The other 6 genuinely
>   have no separator: two `Intro` lines, two interviews, `Grinda & Nic ZigZag` and
>   `Gydra & Ihr Caliban`. The first four are markers now; the last two warn `line-unparsed`.
> - Time forms: `HH:MM:SS` is on **2,382** lines, not 2,112.
> - Year rows (`Effector - 2012`): single-line `parse()` puts the year in `year`, not in a version.
>   The artist name (`Effector`) becomes the title.
> - Dub row (`ez4cheezy - Zhukovskiy dub`): the artist became the **title** (`ez4cheezy`, with a
>   `dub` version), not "a version of the artist".
> - The 3a table contradicts itself: `Anticipation VIP` is listed as the title *and* as a `vip`
>   version, while `Dreamer VIP` is split into `Dreamer` + VIP. Resolved: in tracklist mode the
>   text after the first spaced dash is kept verbatim, so the titles are `Anticipation VIP` and
>   `Dreamer VIP` with no version (R6.10).
> - The `neuropunk/web` regex in section 9 is not anchored and allows `\s{1,3}` (any whitespace)
>   after the time, not `^` and 1–3 spaces. `scripts/corpus-check.mjs` uses the real one.
> - Suggestions: of the 10 real out-of-order times, only **2** have a unique fix, and one of them
>   needs a second search on the **previous** line: `01:22:21` then `01:03:55` is fixed by changing
>   the first to `01:02:21`, not the second to `01:23:55` as 3a guessed (R13.7).
> - Decided differently: the per-line mode defaults to `"tracklist"`, not `"clean"`; U+2028,
>   U+2029 and U+0085 are not line breaks; per-line `ParsedTrack` warnings are not repeated in
>   `tracklist.warnings`; entries have no `warnings` field; no `formatTime` export.
> - Deferred, not in 0.3.0: `timeMeaning` (durations instead of start times), `timePosition`
>   (trailing times), time ranges and `time.end`, `time-gap` / `time-overlap` and `gapThreshold`,
>   `time-meaning-guessed`, cross-line `knownArtists` (draft R13.10), and a label/catalog junk kind.

**Status:** draft requirements, written 2026-10-12 from the Neuropunk platform side. Not yet part of
`SPEC.md`. Follows the repo's rule: spec first, then fixtures, then ports (`AGENTS.md`). The rule
numbers below (R13.x) are placeholders; assign real ones when the text moves into `SPEC.md`.

**Why now.** The Neuropunk platform (`np-platform`, a label management system) stores a free-text
tracklist per release (mixes, podcasts, streams) and wants to parse it **once, on the platform, at save
time**, then ship structured entries to its web player, its app and its backend instead of every
client re-parsing text. It also wants an editor ("tracklist manager") where a person sees every line
parsed, moves times (one track or many), and sees problems before saving. `trackparse` already
understands a single line very well; the README lists "multi-line tracklists and DJ cue sheets" as
planned. This document says what the platform needs from that milestone so the work happens here,
once, in the spec, and every port gets it.

## 1. Goals and non-goals

**Goals**

1. `parseTracklist(text)`: split a block of text into lines and understand each one, using the
   existing `parse()` per line plus tracklist-level context.
2. **The text stays the original.** The result can always be turned back into exactly the input, and
   an edit changes only the lines it touches (see section 5). No wholesale reformatting.
3. Tracklist-level **checks** (times out of order, duplicates, no start at zero, past the end of the
   audio, unknown titles, lines without a time, ...) as structured warnings with line references.
4. A small set of **pure edit functions** on the text: set one line's time, shift times, sort by time,
   replace a line's track part.
5. A stable, schema-validated JSON shape that a consumer can store and send over an API.
6. Same behaviour in the JS and Python ports, from shared fixtures (R0 portability rules apply).

**Non-goals**: fetching anything, audio analysis, matching tracks to a catalogue, fixing artist
spelling, a UI. `parse()` single-line behaviour must not change (no existing `expected` changes, so
this is additive: a minor version).

## 2. What the platform sends and needs back

Typical input (a real Neuropunk mix tracklist, as typed by an editor):

```text
00:00:00 Intro
00:01:07 01. Magnetude - ID
00:02:57 02. Mean Teeth - Airpusher (The Clamps Remix)
00:04:06 03. Dark Soul - Bring The Beat
00:11:41 07. Electrosoul System & Dissident - Throatcut
00:15:33 09. ChaseR & Malstrom - C-LOC
```

The platform needs, per line: the start time in seconds, the position, the artists with roles, the
title, versions (remix and remixer), flags (`ID`), junk, plus **the original line split so that the
time can be rewritten without touching anything else**, and warnings for the tracklist as a whole.

Current single-line results on these lines are already right (time, position, artists, `ID` flagged
`unknownTitle`, `The Clamps` as remixer, `Intro` gets `noSeparator`). The gaps are below.

## 3a. Evidence from real tracklists (read this first)

Source: all **117 tracklists of the NEUROPUNK PODCAST branch** on the Neuropunk staging backend
(`content_release.track_list`, read-only export on 2026-10-12): **5,031 non-blank lines**. Run
through `parse()` and through the regex `neuropunk/web` uses today. Priorities in this document
follow these numbers, not guesses.

| Finding | Number | Consequence |
|---|---|---|
| Lines with a time | 5,031 of 5,031; the web regex and `parse()` agree on the **seconds of every line** | Time parsing parity already holds; the exotic time forms in 3b are **not** seen in practice |
| Time forms | `MM:SS` 2,591 lines, `HH:MM:SS` 2,112 (two-digit hours), `H:MM:SS` 55 (one-digit hours); **72 of 117 lists mix `MM:SS` before one hour with `HH:MM:SS` after**; none uses minutes over 59 | Edits must keep each line's own style; a time that grows past one hour in `MM:SS` style becomes `HH:MM:SS` (or `H:MM:SS` when the list uses one-digit hours), never `61:05` |
| Line endings | 59 lists `\r\n`, 58 `\n`, none mixed; no trailing newline at the end | R13.1/R13.2 (lossless, `eol` per line) are mandatory |
| Trailing spaces | **228 lines** end in spaces (`...Item - 2007 `) | Never trim in `raw`; edits must leave other lines' spaces alone (R13.12) |
| Time out of order | **10 lists**; most are single-digit typos: `00:06:29` after `01:04:39` (should be `01:06:29`), `01:03:55` after `01:22:21` (`01:23:55`), `24:35` after `26:26` | The checker must flag them **and suggest the fix** (4.5, `suggestion`) |
| First time not 0 | 1 list (starts at 00:25) | `no-start-at-zero` as `info` |
| Duplicate times | 0 | still list it, low priority |
| Unknown titles/artists | `ID` 102 lines, `Untitled` 10, `???` 6, `? ` 1; `??? - ???` and `ID - ID` flagged already | Existing flags are right; keep them in the tracklist output |
| Position forms | `NN.` dominant; **bare numbers without a dot** (`8 Linkin Park...`, `40 Optiv & BTK...`, `74 Gydra & Ihr Caliban`) are not recognised as positions; sequences with gaps 2, duplicates 2, numbered/unnumbered mixes 5 | Position by **sequence context** (R13.11) |
| `=>` / `==>` arrows after the time | 3 lines (`00:00 => No Rules ft. Thesys - Wolflair` puts `=> No Rules` into the artist; `01:19:32 => Paperclip interview` is an interview marker) | Arrow is part of `gap`, not the track |
| Markers | `00:00:00 Intro` (2), interviews (`Engage Interview @ Тула`, `=> Paperclip interview`) | line kind `marker` (R13.4) |
| **Single-line rules fight the tracklist convention** | **23 lines** get `noSeparator` and a wrong split although they are plain `Artist - Title` | see below: needs a tracklist mode |

**The tracklist-mode problem (the main finding).** In a tracklist, the first ` - ` is the
artist/title separator on practically every line. Single-line `parse()` sometimes reads it otherwise:

| Line | Today | Expected |
|---|---|---|
| `01:13:14 20. Paperclip - Anticipation VIP` | artist none, title `Paperclip` (the rest read as a `Title - Version` suffix) | artist `Paperclip`, title `Anticipation VIP` (version `vip`) |
| `34:37 10. Daly - Dreamer VIP` | same | artist `Daly`, title `Dreamer` + VIP |
| `01:18:03 27. Electrosoul System - Liquid Metal` | title `Electrosoul System`, **`Liquid Metal` eaten as genre junk** | artist `Electrosoul System`, title `Liquid Metal` |
| `02:07:06 66. NERVƎ - Chillout`, `33:35 19. Sub Mass - Jump Up` | the title is eaten as a genre | title kept |
| `27:28 10. Effector - 2012`, `01:36:20 52. Nphonix & Item - 2007 ` | year read as a version | title `2012` / `2007` |
| `02:10:02 67. ez4cheezy - Zhukovskiy dub` | `dub` read as a version of the artist | artist `ez4cheezy`, title `Zhukovskiy` + dub |
| `00:09:20 8 Linkin Park_-_Points Of Authority (Gydra Bootleg) ` | no separator found (`_-_`), `8` not a position | position 8, artist `Linkin Park`, title `Points Of Authority`, bootleg by Gydra |
| `00:54:49 40 Optiv & BTK-Inception (Gydra remix)` | no separator (`BTK-Inception`, no spaces) | position 40, artist `Optiv & BTK`, title `Inception`, remix by Gydra |
| `01:40:05 74 Gydra & Ihr Caliban` | no separator | no dash at all: keep as title with `line-unparsed` warning, position 74 |

So `parseTracklist` needs a **tracklist mode** (new `mode: "tracklist"` for the track part, the default
there): the first ` - ` / ` – ` (and `_-_`, and a tight `-` between a letter and a letter when most
lines of the list use spaced dashes **only if** there is no spaced dash on the line) is the
artist/title separator; version-by-dash and genre-junk extraction are **not** applied to text after
that separator unless it is in brackets; a bare number before the track text is a position when its
neighbours form a sequence. The decision uses tracklist-level statistics (share of lines with a
spaced dash, position sequence), computed deterministically from the text. This is exactly the
cross-line context R13.10 describes; the examples above are the first fixtures (they are real lines).

## 3b. Probes of synthetic variants (checked against `js/dist`; NOT seen in the real corpus, so lower priority)

| Input line | Today | Needed |
|---|---|---|
| `12:34 \| Noisia - Stigma` | no timestamp; `12:34` becomes junk (`other`) | timestamp 754, `\|` as a time separator |
| `125:30 Noisia - Stigma` | no timestamp; `125:30 Noisia` becomes the artist | timestamp 7530 (minutes over 59 without hours: long mixes) |
| `1:75 Noisia - Stigma` | no timestamp; `1:75 Noisia` becomes the artist | not a time; keep it out of the artist and warn `invalid-time` |
| `00:00:07.500 Noisia - Stigma` | no timestamp; `00:00:07.500 Noisia` is the artist | timestamp 7.5 (fraction kept as given; seconds as a number) |
| `00:00 - 05:30 Noisia - Stigma` | start recognised; `05:30 Noisia` becomes the artist | start 0 and end 330 (`timestamp.end`) |
| `Noisia - Stigma 12:34` / `[12:34]` | time stays in the title | optional trailing time (`timePosition`), see 4.3 |
| `Tracklist:`, `Part 2`, a URL, `Recorded live at Fabric, 2024` | parsed as a title with `noSeparator` | line kind `header` / `link` / `note`, not a track |
| `00:45:00 Break`, `01:00:00 Outro`, `00:00:00 Intro` | title with `noSeparator` | line kind `marker` (kept: it has a time) |
| `00:34:00 Artist - Title [LBL001]` | `[LBL001]` stays in the title | optional: junk kind `label` / `catalog` (nice to have) |

These variants never occur in the 5,031 real lines. Implement them for robustness (other labels, YouTube descriptions), after the items in 3a.

Already fine and must stay byte-identical: `[00:12:34] x`, `(12:34) x`, `12:34 - x`, `0:07 x`,
`12:34   03) x`, `ID - ID`, `Unknown - Unknown`, `? - ?`, `A x B - Mashup`.

## 4. Proposed rules (draft R13)

### 4.1 Line splitting and the lossless guarantee

- **R13.1** Split on LF, CRLF and CR. Each line keeps its own terminator in `eol` (`"\n"`, `"\r\n"`,
  `"\r"`, or `""` for the last line without one). Blank and whitespace-only lines are entries with
  kind `blank` (so the lossless guarantee holds); consumers skip them.
- **R13.2 (lossless)** For every input `s`: `join(entry.raw + entry.eol for each entry) === s`.
  No normalization of whitespace, dashes, quotes or case in `raw`. (`parse()` still normalizes
  internally per R1; that never leaks into `raw`.)
- **R13.3 (parts, no offsets)** R0.5 forbids offsets in the output, so the split of a line is given
  as **strings**: `parts = { lead, time, gap, rest }` with `lead + time + gap + rest === raw`.
  `lead` is whatever precedes the time (`[`, `(`, spaces), `time` the verbatim time text, `gap`
  what follows it up to the track text (`] `, `) `, ` - `, ` | `, spaces), `rest` the remainder
  (position, artists, title, versions). A line without a recognised time has `time: ""` and
  `lead + gap` empty, `rest === raw`. For a time range (4.3) `time` is the start and `gap` ends
  after the end time.

### 4.2 Line kinds

- **R13.4** Each entry has `kind`: `track` (has artists or a title that looks like a track),
  `marker` (a known cue word: intro, outro, break, interlude, jingle, ad/advert, talk, interview,
  intermission, start, end, ...; word list in `spec/data/`, per R12, localisable), `header`
  (`Tracklist:`, `Setlist`, `Part 2`, `Hour 1`, ...), `link` (URL), `note` (any other line with no
  time and no separator), `blank`. A **time on the line wins**: `00:45:00 Break` is a `marker`, not a
  `note`. Rule of thumb: when unsure, `track`, and add a warning (extract, don't delete).
- **R13.5** Only `track` and `marker` entries carry a parsed `track` (the `ParsedTrack` of `rest`,
  with `mode: "clean"` unless options say otherwise) and count toward the time checks. `header`,
  `link`, `note`, `blank` are kept verbatim and never dropped.

### 4.3 Times

- **R13.6 (accepted forms)** `H:MM:SS`, `HH:MM:SS`, `M:SS`, `MM:SS`, with optional fraction
  (`.5`, `.500`, also `,500`), optionally wrapped in `[]` or `()` and followed by one of
  `]`, `)`, `-`, `–`, `—`, `|`, `.`, `:`, or spaces. Minutes over 59 are allowed **only without an
  hours part** (`125:30` = 7530 s), so long mixes written as `MM:SS` work; with hours, minutes and
  seconds must be below 60 (`1:75`, `0:61:00` are `invalid-time`, not a time, and are left in `rest`
  untouched). Fraction is kept as a decimal in `seconds`; `timestamp.raw` stays verbatim.
- **R13.7 (range)** `start - end` (`00:00 - 05:30`, also `00:00-05:30`) gives `time` = start and
  `end` (seconds). The tracklist module may then use `end` for gap/overlap checks.
- **R13.8 (position of the time)** option `timePosition: "start" | "end" | "auto"` (default
  `"start"`: today's behaviour). `"end"` reads a trailing `[12:34]` / `12:34` after the title.
  `"auto"` decides **per tracklist** by majority, so a list is never half-and-half.
- **R13.9 (meaning of the time)** option `timeMeaning: "start" | "duration" | "auto"`. Some lists give
  each track's *length* (`03:45 Artist - Title`) instead of its start. `"auto"` picks `duration`
  when the times are not increasing for most lines and their sum is plausible for the audio length
  (when given); the result is reported in `tracklist.timeMeaning` and, for `duration`, entries get a
  derived `start` (running sum). Never guess silently: add `time-meaning-guessed` when auto decided.

### 4.4 Tracklist-level context

- **R13.10** `knownArtists` for line N includes the artists of all other lines of the same tracklist
  (existing R12 option, filled automatically unless the caller passes its own), so `A & B - Title`
  keeps `A & B` together when `A & B` is known elsewhere, and a split joiner chosen for most lines is
  preferred for ambiguous ones. Deterministic: depends only on the text and options, never on order
  of evaluation.
- **R13.11 (position numbering)** entries with `position` are checked as a sequence (see warnings);
  a list that numbers some lines and not others is allowed (markers and unnumbered lines).

### 4.5 Warnings (`tracklist.warnings[]`, each `{ code, line, related? }`, line is 1-based)

`time-missing` (a track line with no time while others have one), `time-out-of-order` (a start below
the previous start; **with `suggestion`**: the nearest value that restores the order, found by changing the
fewest digits of the raw time, e.g. `00:06:29` between `01:04:39` and `01:08:10` suggests `01:06:29`; no
suggestion when several values tie or none fits), `time-duplicate` (same start twice), `no-start-at-zero` (first timed line is not
0; info level), `time-after-audio-end` (needs `options.audioSeconds`), `time-gap` / `time-overlap`
(with `end`, or with `audioSeconds` and a start far from the next one; threshold option, default off),
`invalid-time` (R13.6), `position-gap`, `position-duplicate`, `time-meaning-guessed`,
`line-unparsed` (a `track` entry with `noSeparator`), plus every per-line `ParsedTrack.warnings`
repeated with its line number. Each warning has a `severity`: `info | warning`. **Warnings never
change `raw` or drop a line.**

## 5. Edit API (pure functions on text)

Needed so a UI can move times without ever reformatting the editor's text.

```ts
parseTracklist(text, options?): Tracklist
applyEdits(text, edits, options?): { text: string; changed: number[]; warnings: ... }
// edits: { op: "set-time", line, seconds }
//      | { op: "shift", seconds, lines?: number[], from?: number }   // from = this line and all after
//      | { op: "sort-by-time" }
//      | { op: "set-rest", line, rest }       // replace parts.rest, e.g. rest = format(track)
//      | { op: "delete", line } | { op: "insert", after, text }
formatTime(seconds, style?): string
```

- **R13.12 (locality)** `applyEdits(text, [])` returns `text` unchanged. An edit changes **only the
  lines it names**; every other line is byte-identical, `eol` included. `changed` lists the 1-based
  line numbers actually rewritten.
- **R13.13 (time style is kept)** `set-time` and `shift` rewrite only `parts.time`, in the style
  that line already has: digit width (`0:07` stays one digit of minutes, `00:07` stays two), hours
  present or not, fraction digits, brackets (outside `time`, so untouched). If the new value no
  longer fits the style it is **upgraded minimally** (`59:59` + 2 s in `MM:SS` style becomes `60:01`
  when the list uses minutes over 59, else `1:00:01`; the rule and the default are an open point,
  section 8). A new line created by `insert` without a time style uses `HH:MM:SS`.
- **R13.14 (clamp)** a shift that would go below 0 clamps that line to 0 and reports
  `time-clamped` (line number); it never produces a negative or invalid time.
- **R13.15 (sort)** `sort-by-time` moves whole original lines (raw + eol handled so the last line
  keeps its own terminator rules); stable for equal times; lines without a time stay attached to the
  previous timed line (headers/notes between tracks move with their block: define with fixtures).
- **R13.16 (round trip)** `shift(+n)` then `shift(-n)` on the same lines returns the original text
  unless a clamp or a style upgrade occurred (then it reports it).
- **R13.17 (totality)** Never throws. Bad edits (line out of range, non-finite seconds) return the
  text unchanged with a warning.

## 6. Output shape (JSON-schema-able)

```jsonc
{
  "specVersion": "0.3.0",
  "timeMeaning": "start",
  "entries": [
    { "line": 2, "kind": "track", "eol": "\n",
      "raw": "00:01:07 01. Magnetude - ID",
      "parts": { "lead": "", "time": "00:01:07", "gap": " ", "rest": "01. Magnetude - ID" },
      "time": { "raw": "00:01:07", "seconds": 67, "end": null },
      "track": { /* ParsedTrack of "01. Magnetude - ID" */ },
      "warnings": [] }
  ],
  "warnings": [ { "code": "time-out-of-order", "line": 9, "severity": "warning" } ],
  "stats": { "lines": 41, "tracks": 38, "markers": 3, "timed": 41, "firstTime": 0, "lastTime": 9670 }
}
```

Every key always present (like `ParsedTrack`). Add `spec/schema/tracklist.schema.json`,
`options` additions in `options.schema.json`. The platform stores this JSON next to the text and
re-derives it whenever the text or the library version changes, so output must be **deterministic and
versioned** (`specVersion`; a patch release never changes an `expected`).

## 7. Options

`mode` (default `"clean"` for the track part), `knownArtists`, `keywords` (R12, as today),
`timePosition`, `timeMeaning`, `audioSeconds` (number, enables `time-after-audio-end`),
`gapThreshold` (seconds, default off), `markerWords` (extend R13.4 list). Invalid options never throw
(R0.8).

## 8. Open points for the owner

1. Style upgrade when a time outgrows its width (R13.13): **answered by the data (3a)**: no real list uses
   minutes over 59 and 72 of 117 mix `MM:SS` with hours, so upgrade to `HH:MM:SS` (or `H:MM:SS` when
   the list's hour times use one digit). Confirm.
2. `sort-by-time` and non-track lines between tracks (R13.15): attach to the previous timed line
   (suggested) or to the next one?
3. `timeMeaning: "auto"`: ship in the first release, or only `start` and an explicit `duration`?
4. Label/catalog junk kind (3b, last row): in this milestone or later?
5. Python port timing: same release (platform only needs JS; the repo rule says ports stay in step).

## 9. Fixtures, properties, ports

- **New fixture type** `tracklist` (schema `fixture-file`: `input` is multi-line text, `expected` the
  partial/exact `Tracklist`, plus optional `edits` and `expectedText` for edit cases). Handwritten
  topics under `spec/fixtures/handwritten/`: `tracklist-lines` (splitting, eol styles, blank/header/
  link/note/marker kinds), `tracklist-times` (every form in 4.3 incl. the synthetic probes of 3b),
  `tracklist-warnings` (each code, positive and near-miss), `tracklist-edits` (set-time, shift,
  sort, clamp, style keep and upgrade, locality), `tracklist-context` (known artists across lines).
  Include the six sample lines in section 2 as a real-world case, and a 120-line mix from the
  platform corpus (see below) as one exact case.
- **Generator:** combinatorial cases for time form x gap x position x line ending x wrapper, expected
  from atoms (never from a parser), as today.
- **Property tests (fast-check / Hypothesis):** (a) lossless: `join(raw+eol) === input` for any string;
  (b) totality: any string and any edit list returns, never throws, 10,000 lines under 200 ms;
  (c) locality: an edit changes only named lines; (d) `applyEdits(t, [])` is identity;
  (e) shift round trip (R13.16); (f) `parts` concatenate to `raw`; (g) determinism: same input and
  options, byte-identical JSON in JS and Python (`compare-ports` covers tracklist fixtures too);
  (h) `parseTracklist` of a single line equals `parse()` of that line on the track part.
- **R0 constraints apply:** no lookaround or `\p{}` regexes, code-point iteration, explicit
  whitespace set (note: R0.2 includes U+2028/2029, so they must also count as line breaks or be
  explicitly excluded; decide and fixture it), digits ASCII only, no offsets in output (hence
  `parts` as strings).
- **Corpus (available now):** the 117 podcast tracklists above are exported at
  `/tmp/claude-1000/-home-matikku-neuropunk-np-platform/6f85d7f4-600c-4ec2-b227-8a47691fa6ae/scratchpad/podcast-tracklists.jsonl`
  (one JSON object per release: `id`, `title`, `released_at`, `track_list`; session scratch space, copy it out
  if you want to keep it). 67 more tracklists exist in other branches (ARCHIVE 44, TAMRECORDS 17, LIVE / STREAMS 3,
  КОСТЁР ЗВУК 3), not yet analysed. The platform will also offer a repeatable text-only export later; each fixture
  mined from it gets `source`.
  Parity target: every line the current `neuropunk/web` regex accepts must yield the same seconds
  (`^(H|HH):MM(:SS)? +1-3 spaces, optional NN. or NN), then text`), and differences must be listed
  and justified (e.g. `1:75` is now `invalid-time`).

## 10. Packaging and release

- Subpath export **`trackparse/tracklist`** (own entry, tree-shakeable) so single-line users do not
  pay for it; the main entry stays as is. Still zero dependencies, ESM + CJS + types; size budget:
  tracklist entry at most +8 kB gzip. Python: `trackparse.tracklist`.
- Spec `0.3.0` (new section, additive), JS and Python `0.3.0` together; conventional commit `feat`.
  No existing fixture `expected` changes (run `pnpm gen:check` and `compare-ports`: 0 differences).
- Update README "At a glance" and the roadmap line ("then tracklists" becomes shipped), and add a
  `tracklist` section to the API docs.

## 11. How the platform will use it (for context, not a requirement)

`np-platform` adds `trackparse` to its core package and wraps `trackparse/tracklist`: on every save
it calls `parseTracklist(text, { audioSeconds })`, stores the result (jsonb) beside the text, and
sends it in the release event; the editor calls the same functions in the browser (live preview,
`applyEdits` for time moves and shifts, warnings in a checks panel). The text stays the source of
truth, so a parser upgrade only means "re-derive and store again". Platform story 19.1 depends on this
release; until it exists nothing on the platform side starts.

## 12. Suggested order of work

1. Decide section 8 and lock the line-splitting and `parts` contract (R13.1–R13.3).
2. Fixtures first, from **real lines** (3a): `tracklist-lines` (CRLF, trailing spaces, markers, arrows), `tracklist-mode` (the 3a table), `tracklist-times` (mixed styles; then the synthetic forms of 3b).
3. JS: splitter, time scanner extension (R13.6–R13.8), line kinds, `parseTracklist`.
4. Warnings and context (R13.10, 4.5), then `applyEdits` with its properties.
5. Python port, `compare-ports`, size and performance checks, docs, release.
