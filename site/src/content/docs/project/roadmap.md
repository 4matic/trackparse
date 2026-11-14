---
title: Limitations and roadmap
description: What trackparse doesn't do yet, and what comes next.
---

The limits below come from the [spec](/trackparse/reference/spec/), so they are the same in the
JavaScript, Python, Rust and Go packages. A new feature starts in the spec and lands in all four at
once.

## Not there yet

- Durations. Some tracklists give each track's length instead of its start time, but times are
  always read as [start times](/trackparse/guides/tracklists/#times) for now.
- Times at the end of a line (`Noisia - Stigma [12:34]`) and time ranges (`00:00 - 05:30`). A
  trailing time stays in the title. In a range only the start becomes the line's time; the end
  ends up in `track.timestamp` and nothing checks it.
- Label and catalog numbers as fields. Catalog numbers like `[HOSP123]` stay in the title.
  Only known label phrases (`[NCS Release]`) go to `junk` as `label`.
- Key and BPM (`8A`, `174 BPM`) stay in the title too.
- An artist database. `Earth, Wind & Fire` splits unless you pass it in
  [`knownArtists`](/trackparse/guides/artists/#knownartists) (`known_artists` in Python and Rust, `KnownArtists` in Go),
  and a tracklist doesn't learn names from its other lines yet.
- Gap and overlap checks between consecutive tracklist times.

If one of these blocks you, [open an issue](https://github.com/4matic/trackparse/issues) with a few
real examples. Real inputs decide how a rule gets written. [Contributing](/trackparse/project/contributing/)
explains what a useful report looks like.

## Next

1. Tracklist features from the list above, starting with durations and trailing times.

The changelogs list what has shipped so far:
[JavaScript](https://github.com/4matic/trackparse/blob/main/js/CHANGELOG.md),
[Python](https://github.com/4matic/trackparse/blob/main/python/CHANGELOG.md),
[Rust](https://github.com/4matic/trackparse/blob/main/rust/CHANGELOG.md),
[Go](https://github.com/4matic/trackparse/blob/main/go/CHANGELOG.md) and the
[spec](https://github.com/4matic/trackparse/blob/main/spec/CHANGELOG.md).
