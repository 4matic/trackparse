---
title: Editing tracklists
description: Fix times, shift whole sections, sort, insert and delete lines, and touch nothing else.
---

`applyEdits(text, edits)` takes a tracklist as text and returns new text. It's built for editors:
you get back the lines it changed, and every other line keeps its bytes and its line ending.

```js
import { applyEdits, parseTracklist } from "trackparse/tracklist";
```

Using the [list from the previous page](/trackparse/guides/tracklists/#a-first-list), apply the
suggested fix, then push everything from line 5 on back by 45 seconds:

```js
const fix = parseTracklist(text).warnings[1].suggestion;   // { line: 8, raw: '01:11:02', seconds: 4262 }

const result = applyEdits(text, [
  { op: "set-time", line: fix.line, seconds: fix.seconds },
  { op: "shift", seconds: 45, from: 5 },
]);

result.changed;   // [ 5, 6, 7, 8, 9 ]
result.warnings;  // [ { code: 'style-upgraded', edit: 1, line: 5 } ]
result.text.split("\r\n").slice(4);
// [
//   '01:00:16 Alix Perez - Fade Away',
//   '01:03:55 Interview with Hybris',
//   '01:08:29 Imanu - Sacrifice',
//   '01:11:47 Current Value feat. Teddy Killerz - Payload',
//   '01:16:21 Hybris - Spinning Top'
// ]
```

Lines 1 to 4 are untouched, CRLF included. Edits run in order, each on the text the previous one
produced, with line numbers as that text has them.

## The edits

| Edit | Effect |
|---|---|
| `{ op: "set-time", line, seconds }` | Rewrite the time on that line |
| `{ op: "shift", seconds, lines?, from? }` | Add `seconds` (negative to go back) to the listed lines, to every timed line from `from` on, or to all of them |
| `{ op: "sort-by-time" }` | Reorder by time. Untimed lines move with the timed line above them, and lines before the first time stay on top |
| `{ op: "set-rest", line, rest }` | Replace everything after the time, for example with `format(track)` |
| `{ op: "insert", after, text }` | Insert a line after line `after` (`0` puts it at the top). It gets the text's usual line ending |
| `{ op: "delete", line }` | Remove a line |

## Times keep their style

A rewritten time looks like its neighbours. Hours or no hours, digit widths and fraction digits are
all kept, so `52:47` plus 45 seconds is `53:32`, not `00:53:32`.

When a time without hours crosses the hour, it gains hours in the style the rest of the list uses
(`59:31` → `01:00:16`, or `1:00:16` if the list writes one-digit hours). The edit reports
`style-upgraded` so you know the line got longer.

## When an edit can't apply

Edits never throw. A bad one changes nothing and reports why:

| Code | When |
|---|---|
| `edit-invalid` | missing line, no time on the line, `NaN` or infinity, a time too large to write, a line break inside `text` or `rest`, an unknown `op` |
| `time-clamped` | the time would go below zero, so it was set to zero |
| `style-upgraded` | a time gained hours (see above) |

Each edit warning is `{ code, edit, line }`, where `edit` is the index in your `edits` array and
`line` is `null` when the edit doesn't name a line.

## Guarantees

The property tests check these on random text and random edit lists:

- `applyEdits(t, [])` returns `t`.
- An edit never touches a line it doesn't name.
- `shift(+s)` followed by `shift(-s)` gives back the original text, unless something was clamped
  or upgraded on the way.

## Python

Same function, snake_case, with plain dicts for edits. The keys are the same as in JS:

```python
from trackparse.tracklist import apply_edits

r = apply_edits(text, [{"op": "set-time", "line": 8, "seconds": 4262},
                       {"op": "shift", "seconds": 45, "from": 5}])
r.changed   # (5, 6, 7, 8, 9)
r.warnings  # (EditWarning(code='style-upgraded', edit=1, line=5),)
```
