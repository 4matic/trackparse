---
title: Limitations and roadmap
description: What trackparse doesn't do yet, and what comes next.
---

## Not there yet

- Durations. Some tracklists give each track's length instead of its start time, but times are
  always read as start times for now.
- Times at the end of a line (`Noisia - Stigma [12:34]`) and time ranges (`00:00 - 05:30`). A
  trailing time stays in the title. In a range only the start becomes the line's time; the end
  ends up in `track.timestamp` and nothing checks it.
- Label and catalogue numbers as fields. Catalogue numbers like `[HOSP123]` stay in the title.
  Only known label phrases (`[NCS Release]`) go to `junk` as `label`.
- Key and BPM (`8A`, `174 BPM`) stay in the title too.
- An artist database. `Earth, Wind & Fire` splits unless you pass it in `knownArtists`, and a
  tracklist doesn't learn names from its other lines yet.
- Gap and overlap checks between consecutive tracklist times.

If one of these blocks you, open an issue with a few real examples. Real inputs are what decide how
a rule gets written.

## Next

1. The Rust port, on crates.io, passing the same fixtures. `serde` support behind a feature
   flag, and `unicode-normalization` as the only required dependency.
2. Tracklist features from the list above, starting with durations and trailing times.

The [changelog](https://github.com/4matic/trackparse/blob/main/js/CHANGELOG.md) lists what has
shipped so far.
