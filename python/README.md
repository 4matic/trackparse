# trackparse

[![PyPI](https://img.shields.io/pypi/v/trackparse?color=3775a9&logo=pypi&logoColor=white)](https://pypi.org/project/trackparse/)
[![Python versions](https://img.shields.io/pypi/pyversions/trackparse?logo=python&logoColor=white)](https://pypi.org/project/trackparse/)
[![python](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/python.yml?branch=main&label=tests&logo=github)](https://github.com/4matic/trackparse/actions/workflows/python.yml)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/4matic/trackparse/blob/main/LICENSE)

**Turn messy track names into structured data: who made it, who's featured, who remixed it, and
what's just noise.**

```text
01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]
```

Most title parsers split that into `"Wilkinson ft. Becky Hill & Tom Cane"` and
`"Afterglow (Sub Focus VIP Remix)"` and stop. Others "clean" it and throw the feat and the remix
away. trackparse keeps all of it. Everything it recognises goes into a field, and anything it
doesn't recognise stays in the title, word for word.

This is the Python port of [trackparse](https://github.com/4matic/trackparse). Its behaviour is
defined by a [shared spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md) and pinned
by 11,891 fixtures. It passes all of them, and for every fixture input its output is byte-identical
to the JavaScript package.

- Python 3.9 to 3.14
- No dependencies
- Typed (`py.typed`), immutable results
- Never raises on string input

## Install

```sh
pip install trackparse
# or
uv add trackparse
```

## Quick start

```python
from trackparse import format_track, parse

track = parse(
    "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]"
)

[f"{a.name} ({a.role})" for a in track.artists]
# ['Wilkinson (primary)', 'Becky Hill (featured)', 'Tom Cane (featured)']
track.title  # 'Afterglow'
track.full_title  # 'Afterglow (Sub Focus VIP Remix)'
track.versions[0].type  # 'remix'
track.versions[0].artists[0].name  # 'Sub Focus'
track.versions[0].modifiers  # ('vip',)
track.junk  # (Junk(raw='Official Video', kind='video'),)
track.position  # Position(raw='01', number=1)
track.mode  # 'youtube'

format_track(track, joiners="canonical")
# 'Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'
```

`to_dict()` returns the spec's JSON form, with the same camelCase keys the JS package uses:

```python
import json

print(json.dumps(track.to_dict(), indent=2))
```

<details>
<summary>Output</summary>

```json
{
  "input": "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
  "mode": "youtube",
  "position": { "raw": "01", "number": 1 },
  "timestamp": null,
  "artists": [
    { "name": "Wilkinson", "role": "primary", "joiner": null, "source": "artist" },
    { "name": "Becky Hill", "role": "featured", "joiner": "ft.", "source": "artist" },
    { "name": "Tom Cane", "role": "featured", "joiner": "&", "source": "artist" }
  ],
  "title": "Afterglow",
  "fullTitle": "Afterglow (Sub Focus VIP Remix)",
  "versions": [
    {
      "type": "remix",
      "raw": "Sub Focus VIP Remix",
      "artists": [{ "name": "Sub Focus", "role": "remixer", "joiner": null, "source": "version" }],
      "modifiers": ["vip"],
      "descriptor": null,
      "year": null,
      "unknownArtist": false,
      "delimiter": "("
    }
  ],
  "year": null,
  "flags": { "explicit": false, "clean": false, "unknownArtist": false, "unknownTitle": false },
  "junk": [{ "raw": "Official Video", "kind": "video" }],
  "warnings": []
}
```

(Reformatted for width; the keys and values are exactly what `to_dict()` returns.)

</details>

## API

```python
parse(input, *, mode="auto", uploader=None, known_artists=(), split_and="auto", keywords=None) -> ParsedTrack
parse_artists(input, *, <same options>) -> list[Artist]
format_track(track, *, feat="source", feat_marker=None, joiners="original",
             versions="all", producers=True, position=False) -> str
normalize(s) -> str
all_artists(track) -> list[Artist]
create_parser(**options) -> Parser   # Parser.parse(...), Parser.parse_artists(...)
SPEC_VERSION, __version__
```

### `parse(input, **options)`

| Option | Default | |
|---|---|---|
| `mode` | `"auto"` | `"clean"`, `"youtube"`, `"filename"`, or `"auto"` to detect from the string |
| `uploader` | `None` | YouTube channel name, used as the artist when there's no separator (`- Topic` and `VEVO` are dropped) |
| `known_artists` | `()` | Names that are never split. There is no built-in list |
| `split_and` | `"auto"` | `"auto"` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. Also `"always"`, `"never"` |
| `keywords` | `None` | Extra vocabulary, added to the built-in lists (see below) |

```python
[a.name for a in parse("Chase & Status - Baddadan").artists]
# ['Chase', 'Status']
[a.name for a in parse("Chase & Status - Baddadan", known_artists=["Chase & Status"]).artists]
# ['Chase & Status']

[a.name for a in parse("Simon and Garfunkel - Mrs. Robinson", split_and="always").artists]
# ['Simon', 'Garfunkel']
```

`keywords` is a dict with any of these keys:

| Key | Type | |
|---|---|---|
| `version_heads` | `dict[str, VersionType]` | Word that sets a version type (`{"rerub": "rework"}`) |
| `descriptors` | `list[str]` | Words kept as a version's `descriptor` |
| `genres` | `list[str]` | Genre words |
| `junk` | `dict[str, JunkKind]` | Phrase to junk kind (`{"some label rip": "label"}`) |
| `feat_markers` | `list[str]` | Extra feat markers |

```python
parse("Artist - Song [Some Label Rip]").title
# 'Song [Some Label Rip]'
parse("Artist - Song [Some Label Rip]", keywords={"junk": {"some label rip": "label"}}).junk
# (Junk(raw='Some Label Rip', kind='label'),)
```

Parsing is total: any `str` gives a `ParsedTrack`, including `""`. Bad option values fall back to
the defaults. Passing something that isn't a `str` raises `TypeError`.

### `ParsedTrack`

| Attribute | Type | |
|---|---|---|
| `input` | `str` | The original string, untouched |
| `mode` | `"clean" \| "youtube" \| "filename"` | Resolved mode |
| `position` | `Position \| None` | Track number: `Position(raw='01', number=1)` |
| `timestamp` | `Timestamp \| None` | Leading cue time: `[12:34]` gives `Timestamp(raw='12:34', seconds=754)` |
| `artists` | `tuple[Artist, ...]` | Primary, featured and producer credits. Remixers live in `versions` |
| `title` | `str` | Clean title. Brackets it doesn't recognise stay in |
| `full_title` | `str` | `title` with the versions re-attached |
| `versions` | `tuple[Version, ...]` | Remixes and other versions, left to right |
| `year` | `int \| None` | From `(2015)` or `- 2015` |
| `flags` | `Flags` | `explicit`, `clean`, `unknown_artist`, `unknown_title` |
| `junk` | `tuple[Junk, ...]` | What was taken out, each with a `kind` |
| `warnings` | `tuple[Warning, ...]` | Heuristics that fired |

`Artist(name, role, joiner, source)`:

- `role` is `"primary"`, `"featured"`, `"remixer"` or `"producer"`.
- `joiner` is the text before the name as written (`"&"`, `","`, `"ft."`, `"prod. by"`), or `None`
  for the first name in a list.
- `source` is where the credit came from: `"artist"` (left of the dash), `"title"` (right of it)
  or `"version"`.

`Version(type, raw, artists, modifiers, descriptor, year, unknown_artist, delimiter)`:

```python
parse("Pendulum - Watercolour (Extended VIP Mix)").versions[0]
# Version(type='vip', raw='Extended VIP Mix', artists=(), modifiers=('extended',),
#         descriptor=None, year=None, unknown_artist=False, delimiter='(')

parse("Queen - Bohemian Rhapsody (Live at Wembley 1986)").versions[0]
# Version(type='live', raw='Live at Wembley 1986', artists=(), modifiers=(),
#         descriptor='at Wembley', year=1986, unknown_artist=False, delimiter='(')
```

Version types, junk kinds and warnings are listed in the
[main README](https://github.com/4matic/trackparse#api) and typed as `Literal` aliases
(`VersionType`, `JunkKind`, `Warning`, `ArtistRole`, `ArtistSource`, `Mode`, `ModeOption`,
`VersionDelimiter`).

### `to_dict()` and `from_dict()`

Every result class has `to_dict()`, which returns plain dicts and lists matching
[`parsed-track.schema.json`](https://github.com/4matic/trackparse/blob/main/spec/schema/parsed-track.schema.json)
(`fullTitle`, `unknownArtist`, ...). With `json.dumps(track.to_dict(), separators=(",", ":"),
ensure_ascii=False)` the bytes are the same as `JSON.stringify(parse(s))` in JS. The `from_dict()`
classmethod goes the other way:

```python
ParsedTrack.from_dict(track.to_dict()) == track  # True
```

### `parse_artists(input, **options)`

Splits a bare artist string. Takes the same options as `parse`.

```python
from trackparse import parse_artists

[f"{a.name}:{a.role}" for a in parse_artists("Sub Focus, Wilkinson & Dimension feat. Kojo")]
# ['Sub Focus:primary', 'Wilkinson:primary', 'Dimension:primary', 'Kojo:featured']
```

### `format_track(track, **options)`

Renders a track back to a string.

| Option | Default | |
|---|---|---|
| `feat` | `"source"` | Where featured artists go: `"source"` (where they were), `"artist"`, `"title"` or `"omit"` |
| `feat_marker` | `None` | Marker for featured artists, such as `"feat."` |
| `joiners` | `"original"` | `"original"` keeps the joiners as written, `"canonical"` uses the standard ones |
| `versions` | `"all"` | `"all"` or `"none"` |
| `producers` | `True` | Include `(prod. ...)` credits |
| `position` | `False` | Include the track number |

```python
from trackparse import format_track

format_track(
    parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus Remix)"),
    feat="title",
    joiners="canonical",
)
# 'Wilkinson - Afterglow (feat. Becky Hill) (Sub Focus Remix)'

format_track(track, feat="omit", versions="none")
# 'Wilkinson - Afterglow'
format_track(track, position=True)
# '01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'
```

### `normalize(s)`

The Unicode cleanup every function runs first: NFC, invisible characters, and dash, bracket and
quote variants.

```python
from trackparse import normalize

normalize("Noisia​ – Stigma （VIP）")
# 'Noisia - Stigma (VIP)'
```

### `all_artists(track)`

Credits plus every remixer, deduplicated.

```python
from trackparse import all_artists

[(a.name, a.role) for a in all_artists(track)]
# [('Wilkinson', 'primary'), ('Becky Hill', 'featured'), ('Tom Cane', 'featured'), ('Sub Focus', 'remixer')]
```

### `create_parser(**options)`

Compiles the options once, for many calls. Per-call options are merged over the parser's: a value
passed per call replaces the base one (`known_artists` included), `None` leaves it alone, and
`keywords` dicts are merged.

```python
from trackparse import create_parser

parser = create_parser(
    known_artists=["Chase & Status", "Camo & Krooked"],
    keywords={"version_heads": {"rerub": "rework"}},
)
parser.parse("Chase & Status x Camo & Krooked - Track (Alix Perez Rerub)").versions[0].type
# 'rework'
```

### `SPEC_VERSION`

The spec version this build implements. The PyPI package, the npm package and the spec are
released together and always share one version number.

## Modes

| Mode | For | Adds |
|---|---|---|
| `clean` | Store metadata, tags | Only a spaced dash separates artist and title |
| `youtube` | Video titles, scrobbles | Pipe segments, `"Title" by Artist`, `Artist: "Title"`, unspaced dashes, trailing junk, uploader fallback |
| `filename` | Files | Strips the extension, `_` becomes a space, `01-Track` numbering |
| `auto` | Anything | `filename` for known extensions, `youtube` when it sees junk, pipes or platform suffixes, `clean` otherwise |

```python
t = parse('"Numb" by Linkin Park', mode="youtube")
[a.name for a in t.artists], t.title, t.warnings
# (['Linkin Park'], 'Numb', ('bySplit',))

t = parse("07_Burial_-_Archangel.mp3")
t.mode, [a.name for a in t.artists], t.title, t.position
# ('filename', ['Burial'], 'Archangel', Position(raw='07', number=7))

parse("Shelter", mode="youtube", uploader="Porter Robinson").artists
# (Artist(name='Porter Robinson', role='primary', joiner=None, source='artist'),)
```

## Typing

The package ships `py.typed` and passes `mypy --strict`. `ParsedTrack`, `Artist`, `Version`,
`Junk`, `Flags`, `Position` and `Timestamp` are frozen dataclasses with snake_case attributes.
Sequences are tuples, so results are immutable and hashable and can go in sets or be used as dict
keys. Option dicts have `TypedDict` types (`ParseOptions`, `KeywordOptions`, `FormatOptions`).

## Development

From `python/` in the [repository](https://github.com/4matic/trackparse), with
[uv](https://docs.astral.sh/uv/):

```sh
uv sync
uv run python scripts/gen_data.py --check  # spec/data codegen is up to date
uv run ruff check . && uv run ruff format --check .
uv run mypy --strict src
uv run pytest                              # every shared fixture, properties, API
uv run python scripts/conformance.py       # pass rate per fixture file and per rule
```

Modules mirror the JS reference one to one (`_scanner.py` is `scanner.ts`, and so on) and cite the
same spec rule IDs. Behaviour changes start in the spec, not here: see
[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md).

## Links

- [Spec](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md)
- [Project README](https://github.com/4matic/trackparse#readme), with a table of real outputs
- [Changelog](https://github.com/4matic/trackparse/blob/main/python/CHANGELOG.md)
- [npm package](https://www.npmjs.com/package/trackparse)

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE)
