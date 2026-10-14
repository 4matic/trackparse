---
title: Tracklist API
description: parseTracklist, applyEdits and every field they return.
---

```js
import { applyEdits, parseTracklist } from "trackparse/tracklist";
// CommonJS: const { applyEdits, parseTracklist } = require("trackparse/tracklist");
```

```python
from trackparse.tracklist import apply_edits, parse_tracklist
```

Neither function throws on string input. The guides [Tracklists](/trackparse/guides/tracklists/)
and [Editing tracklists](/trackparse/guides/editing-tracklists/) walk through examples.

## parseTracklist(text, options?)

Returns a `Tracklist`. Every key is always present.

```ts
{
  specVersion: string;
  entries: TracklistEntry[];
  warnings: TracklistWarning[];
  stats: { lines, tracks, markers, timed, firstTime, lastTime };
}
```

### TracklistEntry

One per line, in order.

| Field | Type | |
|---|---|---|
| `line` | `number` | 1-based |
| `kind` | `"track" \| "marker" \| "header" \| "link" \| "note" \| "blank"` | See [line kinds](/trackparse/guides/tracklists/#line-kinds) |
| `eol` | `"\n" \| "\r\n" \| "\r" \| ""` | The line's own terminator. `""` on a last line without one |
| `raw` | `string` | The line exactly as written, never normalised |
| `parts` | `{ lead, time, gap, rest }` | `raw` cut into four strings that join back into it |
| `time` | `{ raw, seconds } \| null` | The start time |
| `track` | `ParsedTrack \| null` | Parsed from `rest` in tracklist mode. Only `track` and `marker` lines have one |

### TracklistWarning

```ts
{ code: TracklistWarningCode; line: number; severity: "info" | "warning"; suggestion: { line, raw, seconds } | null }
```

Sorted by line. The codes are listed under [Warnings](/trackparse/reference/warnings/#tracklist-warnings).
Warnings from each line's own `ParsedTrack` stay on `entries[i].track.warnings` and aren't repeated
here.

### stats

| Field | |
|---|---|
| `lines` | number of entries |
| `tracks`, `markers` | entries of those kinds |
| `timed` | entries with a time |
| `firstTime`, `lastTime` | seconds of the first and last time, or `null` |

## applyEdits(text, edits, options?)

Returns `{ text, changed, warnings }`.

| Field | |
|---|---|
| `text` | the edited text |
| `changed` | final line numbers of every line an edit wrote, ascending |
| `warnings` | `{ code, edit, line }[]`: `edit` is the index in `edits`, `line` may be `null` |

### Edits

| Edit | Effect |
|---|---|
| `{ op: "set-time", line, seconds }` | Rewrite the time on that line |
| `{ op: "shift", seconds, lines?, from? }` | Add `seconds` to the listed lines, to every timed line from `from` on, or to all of them |
| `{ op: "sort-by-time" }` | Stable sort by time. Untimed lines move with the timed line above them |
| `{ op: "set-rest", line, rest }` | Replace everything after the time |
| `{ op: "insert", after, text }` | Insert a line after `after` (`0` = at the top) |
| `{ op: "delete", line }` | Remove a line |

Edits run in sequence, each on the result of the one before. Times are written in their line's
style, up to 100 hours (or 999 minutes for lists that never use hours).

### Edit warnings

| Code | When |
|---|---|
| `style-upgraded` | a time without hours passed one hour and gained them |
| `time-clamped` | a time would go below zero and was set to zero |
| `edit-invalid` | the edit could not apply and changed nothing |

## Options

Both functions take the same options. `applyEdits` accepts them for symmetry; edits don't depend on
them.

| Option | Default | |
|---|---|---|
| `mode` | `"tracklist"` | Mode for each line's `parse()` |
| `knownArtists`, `splitAnd`, `keywords` | | As for [`parse()`](/trackparse/reference/options/) |
| `audioSeconds` | – | Length of the audio. Enables `time-after-audio-end` |
| `markerWords` | `[]` | Extra marker words |

Python uses keyword arguments (`audio_seconds=`, `marker_words=`).

## Types

Exported from `trackparse/tracklist`: `Tracklist`, `TracklistEntry`, `EntryKind`, `EntryParts`,
`Eol`, `TracklistTime`, `TimeSuggestion`, `TracklistStats`, `TracklistWarning`,
`TracklistWarningCode`, `TracklistOptions`, `Edit`, `EditResult`, `EditWarning`,
`EditWarningCode`. The JSON Schema is
[`tracklist.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/tracklist.schema.json).
