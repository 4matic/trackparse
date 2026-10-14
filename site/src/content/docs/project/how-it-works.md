---
title: How it works
description: The parsing pipeline, why it has no regex lookaround, and how tracklists sit on top of it.
---

trackparse is a fixed pipeline of small hand-written scanners. There are no regexes with lookaround
or backreferences and no backtracking, so it runs in linear time and ports to Python and Rust with
the same output.

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

## Brackets first

Before anything is split, every bracket group is swapped for a placeholder. That's what keeps a `&`
inside `(Chase & Status Remix)` from breaking the artist list, and a dash inside `[Drum - Bass]` from
being taken as the separator.

Each group is then classified on its own: junk, feat, producer, flag, year, version, or unknown.
Unknown groups go back into the title, untouched. This is where "extract, don't delete" lives in
the code: there's no path that drops an unknown group.

## Finding the separator

The separator search is the part that differs most between modes. In `clean` mode only a spaced
dash counts, and dash suffixes that look like versions (`- Remastered 2015`) are peeled off the end
first. `youtube` adds pipes, quoted titles, `"Title" by Artist` and unspaced dashes. `filename`
adds underscores and `01-Track` numbering. Each fallback leaves a [warning](/trackparse/reference/warnings/),
so you can always tell a confident split from a guess.

## Words come from data

Every word list (joiners, feat markers, version keywords, junk phrases, genres, extensions) lives in
JSON files under [`spec/data`](https://github.com/4matic/trackparse/tree/main/spec/data). The ports
compile them into source at build time. No keyword is hard-coded in a scanner, which is why
`keywords` in the options can extend any of them.

## Portability rules

The spec opens with rule R0, which exists so that every port gives the same bytes:

- Whitespace is an explicit list of code points, never a language's `\s` or `trim()`.
- Keywords compare with ASCII-only lowercasing. Full Unicode folding is used only for
  deduplicating names.
- Strings are walked by code point, not by UTF-16 unit or byte.
- A "digit" is `0`–`9` and nothing else.
- Any string must produce a result. Nothing throws, ever.

## Tracklists on top

[Tracklists](/trackparse/guides/tracklists/) (R13) reuse the whole pipeline. The text is split into
lines, keeping each line's terminator. A small scanner reads the time at the start of each line, and
the rest goes through `parse()` in tracklist mode.

Two decisions look across lines. A bare track number is only accepted when a neighbour continues
the sequence. And an unspaced dash only splits when at least half the list uses spaced dashes. To
make both decisions without parsing everything twice, each line first gets a cheap probe (time,
normalisation, prefixes), then one real parse. The checks run last, over the finished entries.

The full rules are in the [spec](/trackparse/reference/spec/).
