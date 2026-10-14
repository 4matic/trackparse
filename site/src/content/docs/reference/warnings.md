---
title: Warnings
description: Every warning trackparse can raise, for single tracks and for tracklists.
---

trackparse never fails, but it does tell you when it had to guess. Warnings are how.

## Track warnings

`ParsedTrack.warnings` is a list of strings. An empty list means every decision was unambiguous.

| Warning | Means | Example |
|---|---|---|
| `noSeparator` | No artist/title separator was found. The whole string is the title | `Around the World` |
| `ambiguousSeparator` | A pipe-separated YouTube title had two parts and no dash, so they were read as artist and title | `Noisia \| Stigma` |
| `unspacedDashSplit` | Split at a dash without spaces around it (youtube and filename modes, and timed tracklist lines) | `Blink-182-All The Small Things` |
| `asymmetricDashSplit` | Split at a dash with a space on one side only | `Artist -Title` |
| `bySplit` | Read `"Title" by Artist` (youtube mode) | `"Numb" by Linkin Park` |
| `quotedTitleSplit` | Took a quoted span as the title and what came before it as the artist (youtube mode) | `Linkin Park: "Numb"` |
| `ambiguousMixCredit` | `<Name> Mix` was read as a remix, but the name is long enough that it might be a style | `(Fonetick Lee & Sonic Art Space Drums Mix)` |
| `unbalancedBrackets` | A bracket was opened and never closed | `Noisia - Stigma (VIP` |

What to do with them depends on your data. For a music library, a track with any warning is a good
candidate for a human to look at. For scrobbling, most of them are fine to accept.

## Tracklist warnings

`Tracklist.warnings` holds objects: `{ code, line, severity, suggestion }`.

| Code | Severity | When |
|---|---|---|
| `time-missing` | warning | A track line has no time while other lines do |
| `time-out-of-order` | warning | Earlier than the previous timed line. May carry a `suggestion` |
| `time-duplicate` | warning | Same time as the previous timed line |
| `no-start-at-zero` | info | The first time isn't `0:00` |
| `time-after-audio-end` | warning | Later than `audioSeconds` |
| `invalid-time` | warning | Looks like a time but isn't one (`1:75`, `0:61:00`). The line keeps it in `rest` |
| `position-gap` | warning | A track number isn't the previous one plus 1 |
| `position-duplicate` | warning | Same track number as the previous one |
| `line-unparsed` | warning | A track line with no artist/title separator, or nothing after the time |

### Suggestions

A `suggestion` is `{ line, raw, seconds }`: the one time, one or two digits away from what was
written, that puts the list back in order. It's written in the same style as the original.

The search runs in two steps. First it tries the flagged line itself. If no time fits there, it
tries the previous timed line, since sometimes that's where the typo is:
`01:22:21` followed by `01:03:55` suggests changing the first line to `01:02:21`. If more than one
candidate fits, or none does, `suggestion` is `null`. A wrong suggestion would be worse than none.

## Edit warnings

`applyEdits` returns `{ code, edit, line }` objects: `style-upgraded`, `time-clamped` and
`edit-invalid`. See [Editing tracklists](/trackparse/guides/editing-tracklists/#when-an-edit-cant-apply).
