---
title: How it works
description: The parsing pipeline, why it has no regex lookaround, and how tracklists sit on top of it.
---

trackparse is a fixed pipeline of small hand-written scanners. There are no regexes with lookaround
or backreferences, so nothing backtracks. A 10,000-code-point input takes at most tens of
milliseconds in JavaScript, Rust and Go, and well under a second in Python.
The same design is what lets the JavaScript, Python, Rust and Go packages give the same output: each
step is a short function that reads the same way in all four languages.

Each step below is a rule group in the [spec](/trackparse/reference/spec/):

```text
input
  └─ normalize          Unicode NFC, invisible characters, dash/quote/bracket variants   (R1)
      └─ mode prelude   file extension, pipes, platform suffixes, uploader               (R2)
          └─ prefixes   track number, cue time                                           (R4)
              └─ groups brackets swapped for placeholders, each group classified         (R3, R5)
                  └─ separator   find the artist/title split                             (R6)
                      ├─ artist side   joiners, features                                 (R7)
                      └─ title side    versions, junk, title credits                     (R8)
                          └─ assemble   dedupe credits, build the ParsedTrack            (R9)
```

The tree above runs inside [`parse`](/trackparse/reference/parse/); [`normalize`](/trackparse/reference/functions/#normalizeinput)
exposes its first step on its own. Every port keeps one source file per step, with the same rule
IDs in the comments:
[`js/src`](https://github.com/4matic/trackparse/tree/main/js/src),
[`python/src/trackparse`](https://github.com/4matic/trackparse/tree/main/python/src/trackparse),
[`rust/src`](https://github.com/4matic/trackparse/tree/main/rust/src) and
[`go`](https://github.com/4matic/trackparse/tree/main/go).

## Brackets first

Before anything is split, every bracket group is swapped for a placeholder. That's what keeps a `&`
inside `(Chase & Status Remix)` from breaking the artist list, and a dash inside `[Drum - Bass]` from
being taken as the separator.

Each group is then classified on its own: junk, feat, producer, flag, year, version, or unknown.
Unknown groups go back into the title, untouched. This is where "extract, don't delete" lives in
the code: there's no path that drops an unknown group. The scanner is
[R3](/trackparse/reference/spec/#r3-group-scanner-and-skeleton) and the classification is
[R5](/trackparse/reference/spec/#r5-group-classification).

## Finding the separator

The separator search is the part that differs most between modes. In `clean` mode only a spaced
dash counts, and dash suffixes that look like versions (`- Remastered 2015`) are peeled off the end
first. `youtube` adds pipes, quoted titles, `"Title" by Artist` and unspaced dashes. `filename`
adds underscores and `01-Track` numbering. Each fallback leaves a
[warning](/trackparse/reference/warnings/), so you can always tell a confident split from a guess.
The search is [R6](/trackparse/reference/spec/#r6-artisttitle-separator), and
[Modes](/trackparse/guides/modes/) shows the difference on real strings.

## Words come from data

Every word list (joiners, feat markers, version keywords, junk phrases, genres, extensions) lives in
JSON files under [`spec/data`](https://github.com/4matic/trackparse/tree/main/spec/data). The ports
compile them into source at build time. No keyword is hard-coded in a scanner, which is why
[`keywords`](/trackparse/reference/options/#keywords) in the options can extend any of them.

## Portability rules

The spec opens with rule [R0](/trackparse/reference/spec/#r0-portability), which exists so that
every port gives the same bytes:

- Whitespace is an explicit list of code points, never a language's `\s` or `trim()`
  ([R0.2](/trackparse/reference/spec/#r0-portability:~:text=R0.2%20Whitespace%20means)).
- Keywords compare with ASCII-only lowercasing ([R0.3](/trackparse/reference/spec/#r0-portability:~:text=R0.3%20Keyword%20comparison)). Full
  Unicode folding is used only for deduplicating names ([R0.4](/trackparse/reference/spec/#r0-portability:~:text=R0.4%20Dedup%20key)).
- Strings are walked by code point, not by UTF-16 unit or byte ([R0.5](/trackparse/reference/spec/#r0-portability:~:text=R0.5%20Iterate%20by)).
- A "digit" is `0` to `9` and nothing else.
- "Letter" and "uppercase letter" mean the Unicode general categories L and Lu, not whatever a
  language's `isalpha` accepts ([R0.6](/trackparse/reference/spec/#r0-portability:~:text=R0.6%20Letter%20means)).
- Any string must produce a result. Nothing throws or panics ([R0.8](/trackparse/reference/spec/#r0-portability:~:text=R0.8%20Parsing%20is)).

[Ports](/trackparse/project/ports/) lists the places where a language's standard library disagreed
with these rules.

## Tracklists on top

[Tracklists](/trackparse/guides/tracklists/) ([R13](/trackparse/reference/spec/#r13-tracklists)) reuse the whole pipeline. The text is split into
lines, keeping each line's terminator. A small scanner reads the time at the start of each line, and
the rest goes through [`parse()`](/trackparse/reference/parse/) in tracklist mode.

Two decisions look across lines. A bare track number is only accepted when a neighbour continues
the sequence. And an unspaced dash only splits when at least half the list uses spaced dashes. To
make both decisions without parsing everything twice, each line first gets a cheap probe (time,
normalization, prefixes), then one real parse. The checks run last, over the finished entries.

The tracklist rules are [R13](/trackparse/reference/spec/#r13-tracklists).

## See also

- [The spec](/trackparse/reference/spec/) for the full rules.
- [How it's tested](/trackparse/project/testing/) for the fixtures and the cross-port diff.
- [Ports](/trackparse/project/ports/) for the four implementations.
