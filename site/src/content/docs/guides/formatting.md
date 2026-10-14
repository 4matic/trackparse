---
title: Formatting
description: Turn a ParsedTrack back into a string, in the style you want.
---

`format(track, options?)` renders a parsed track back to a string. Use it to normalise a messy
library to one style, or to build a display title without the junk.

```js
import { format, parse } from "trackparse";

const t = parse(
  "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
);

format(t);
// 'Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'
```

Junk is never written back. Everything else is, by default, the way it was written.

## Options

| Option | Values | Default |
|---|---|---|
| `feat` | `"source"` keep features where they were, `"artist"` move them left of the dash, `"title"` move them into the title, `"omit"` drop them | `"source"` |
| `featMarker` | the word to write before features | the one from the input |
| `joiners` | `"original"` as written, `"canonical"` normalised (`and` → `&`, `;` → `,`, `ft.` → `feat.`) | `"original"` |
| `versions` | `"all"` or `"none"` | `"all"` |
| `producers` | write `(prod. by …)` credits | `true` |
| `position` | write the track number back | `false` |

```js
format(t, { feat: "title" });
// 'Wilkinson - Afterglow (ft. Becky Hill & Tom Cane) (Sub Focus VIP Remix)'

format(t, { feat: "title", joiners: "canonical" });
// 'Wilkinson - Afterglow (feat. Becky Hill & Tom Cane) (Sub Focus VIP Remix)'

format(t, { feat: "artist", featMarker: "featuring" });
// 'Wilkinson featuring Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'

format(t, { feat: "omit" });       // 'Wilkinson - Afterglow (Sub Focus VIP Remix)'
format(t, { versions: "none" });   // 'Wilkinson ft. Becky Hill & Tom Cane - Afterglow'
format(t, { position: true });     // '01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)'

format(parse("Future - Mask Off (prod. by Metro Boomin)"), { producers: false });
// 'Future - Mask Off'
```

In Python the function is `format_track`, with keyword arguments:
`format_track(t, feat="title", joiners="canonical")`.

## Round trip

Parsing the formatted string gives back the same artist names and roles, the same title and the
same version types and remixers, as long as the first parse raised no warnings. The spec makes this
an invariant (R11.6), and the property tests check it on generated input:

```js
parse(format(parse(s)))  // same artists, title and versions as parse(s)
```

The [playground](/trackparse/playground/) has a `format()` panel if you want to try the options on
your own strings.
