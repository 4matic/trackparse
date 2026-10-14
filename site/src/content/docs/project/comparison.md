---
title: Compared to others
description: How trackparse differs from get-artist-title, metadata-filter and track_parser.
---

| | trackparse | [get-artist-title](https://github.com/goto-bus-stop/get-artist-title) | [metadata-filter](https://github.com/web-scrobbler/metadata-filter) | [track_parser](https://rubygems.org/gems/track_parser) |
|---|---|---|---|---|
| Output | structured object | `[artist, title]` | cleaned strings | artists, feat, remixer |
| Artists as a list with roles | yes | no | no | `&` / `and` only |
| Remixers and version types | 27 types | no | removed | raw remix name |
| Noise | returned with a kind | removed | removed | no |
| Tracklists | yes | no | no | no |
| Languages | JS, Python, one spec | JS | JS | Ruby |
| Last release | 2026 | 2021 | active | 2015 |

These libraries are good at what they set out to do. If you need a clean string to scrobble,
metadata-filter is the better fit, and the Web Scrobbler team maintains it. get-artist-title's
YouTube test cases helped shape several rules here, and its inputs are part of the regression
fixtures (MIT licensed, credited in
[THIRD_PARTY_NOTICES.md](https://github.com/4matic/trackparse/blob/main/THIRD_PARTY_NOTICES.md)).

trackparse is for when a clean string isn't enough and you need to know who did what: which names
are features, who remixed it, what kind of version it is, and what was cut out of the title.
