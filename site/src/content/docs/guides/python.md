---
title: Python
description: The Python package. Same parser, same output as JavaScript, with snake_case functions and frozen dataclasses.
---

```sh
pip install trackparse   # or: uv add trackparse
```

Python 3.9 to 3.14, no dependencies, fully typed. The Python package isn't a wrapper around the
JavaScript one. It's a separate implementation that passes the same 12,965 fixtures, and a script
in CI compares the complete output of both on every fixture input. The difference count is zero.

## Single tracks

```python
from trackparse import format_track, parse, parse_artists

track = parse("Noisia feat. Foreign Beggars - Shellshock")
track.artists
# (Artist(name='Noisia', role='primary', joiner=None, source='artist'),
#  Artist(name='Foreign Beggars', role='featured', joiner='feat.', source='artist'))

parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)").versions[0]
# Version(type='live', raw='Live at Wembley 1986', artists=(), modifiers=(),
#         descriptor='at Wembley', year=1986, unknown_artist=False, delimiter='(')

[f"{a.name}:{a.role}" for a in parse_artists("Sub Focus, Wilkinson & Dimension feat. Kojo")]
# ['Sub Focus:primary', 'Wilkinson:primary', 'Dimension:primary', 'Kojo:featured']

format_track(parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"), feat="title", joiners="canonical")
# 'Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)'
```

Options are keyword arguments in snake_case:

```python
parse(s, mode="youtube", known_artists=["Chase & Status"], split_and="never", uploader="Daft Punk - Topic")
```

## Names

| JavaScript | Python |
|---|---|
| `parse` | `parse` |
| `parseArtists` | `parse_artists` |
| `format` | `format_track` |
| `normalize` | `normalize` |
| `allArtists` | `all_artists` |
| `createParser` | `create_parser` |
| `parseTracklist` | `parse_tracklist` (in `trackparse.tracklist`) |
| `applyEdits` | `apply_edits` (in `trackparse.tracklist`) |
| `fullTitle`, `unknownArtist`, … | `full_title`, `unknown_artist`, … |

## Results are dataclasses

`ParsedTrack`, `Artist`, `Version`, `Junk`, `Flags`, `Position` and `Timestamp` are frozen
dataclasses with snake_case attributes. Sequences are tuples, so results are immutable and hashable.
You can put them in sets or use them as dict keys.

`to_dict()` gives you the spec's JSON with camelCase keys:

```python
import json

json.dumps(track.to_dict(), separators=(",", ":"), ensure_ascii=False)
# the same bytes as JSON.stringify(parse(s)) in JavaScript
```

The single-track classes also have `from_dict()`, which goes the other way.

## Tracklists

```python
from trackparse.tracklist import apply_edits, parse_tracklist

tl = parse_tracklist(text, audio_seconds=4800, marker_words=["drop"])
tl.warnings[1].suggestion
# Suggestion(line=8, raw='01:11:02', seconds=4262)

apply_edits(text, [{"op": "shift", "seconds": -3000, "lines": [2, 3]}]).warnings
# (EditWarning(code='time-clamped', edit=0, line=2),)
```

Edits are plain dicts with the same keys as in JS, including `"from"`. The tracklist results
(`Tracklist`, `TracklistEntry`, `Parts`, `TimeValue`, `Suggestion`, `Stats`, `EditResult`) are frozen
dataclasses too, with `to_dict()`.

## Typing

The package ships `py.typed` and passes `mypy --strict`. Option dicts have `TypedDict` types
(`ParseOptions`, `KeywordOptions`, `FormatOptions`), and the string enums are `Literal` aliases
(`VersionType`, `JunkKind`, `Warning`, `Mode`, `EntryKind`, `TracklistWarningCode`, …).

Every function raises `TypeError` if the input isn't a `str`. Anything else, including bad options,
falls back to defaults rather than raising.
