# trackparse for C and C++

[![tests](https://img.shields.io/github/actions/workflow/status/4matic/trackparse/c.yml?branch=main&label=tests&logo=github)](https://github.com/4matic/trackparse/actions/workflows/c.yml)
[![release](https://img.shields.io/github/v/tag/4matic/trackparse?filter=c-v*&label=release&logo=c&color=a8b9cc)](https://github.com/4matic/trackparse/releases)
[![C11](https://img.shields.io/badge/C-11-a8b9cc)](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.h)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599c)](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.hpp)
[![license](https://img.shields.io/badge/license-MIT-blue)](https://github.com/4matic/trackparse/blob/main/c/LICENSE)

**[Documentation](https://4matic.github.io/trackparse/)** · **[Getting started](https://4matic.github.io/trackparse/start/getting-started/)** · **[Playground](https://4matic.github.io/trackparse/playground/)** · **[Header](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.h)** · **[Changelog](https://github.com/4matic/trackparse/blob/main/c/CHANGELOG.md)**

**Turn messy track names into structured data: who made it, who's featured, who remixed it, and
what's just noise.**

```text
01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]
```

Most title parsers split that into `"Wilkinson ft. Becky Hill & Tom Cane"` and
`"Afterglow (Sub Focus VIP Remix)"` and stop. Others "clean" it and throw the feat and the remix
away. trackparse keeps all of it. Everything it recognizes goes into a field, and anything it
doesn't recognize stays in the title, word for word
([try it in the playground](https://4matic.github.io/trackparse/playground/#track=01.%20Wilkinson%20ft.%20Becky%20Hill%20%26%20Tom%20Cane%20-%20Afterglow%20%28Sub%20Focus%20VIP%20Remix%29%20%5BOfficial%20Video%5D)).

This is the C port of [trackparse](https://github.com/4matic/trackparse), with a header-only C++17
wrapper. Its behaviour is defined by a [shared spec](https://4matic.github.io/trackparse/reference/spec/)
and pinned by [13,130 fixtures](https://4matic.github.io/trackparse/project/testing/). It passes
all of them, and for every fixture input its output is byte-identical to the
[JavaScript, Python, Rust and Go packages](#other-languages).

- C11 for the library, C++17 for [`trackparse.hpp`](#c-wrapper); GCC, Clang and MSVC
- No dependencies beyond the C standard library, `libm` and the system's threads (pthreads, or
  Windows' own locks)
- No regular expressions, and nothing to initialize before the first call
- Every function is total: any input gives a result, and the only failure is running out of
  memory, reported as `NULL` ([Out of memory](#out-of-memory))
- The output does not depend on the compiler, the CPU or the process locale

## Install

**Docs:** [Getting started](https://4matic.github.io/trackparse/start/getting-started/)

There is no package registry for C. Each release attaches two files to its
[GitHub Release](https://github.com/4matic/trackparse/releases) (tags `c-vX.Y.Z`, the first is
`c-v0.7.0`), with a `SHA256SUMS` file next to them:

- `trackparse-c-X.Y.Z.tar.gz`: this folder, for CMake;
- `trackparse-c-X.Y.Z-amalgamation.zip`: `trackparse.c`, `trackparse.h` and `trackparse.hpp`, to
  copy into a project.

Release files are never replaced: a fix is a new version, so a pinned hash keeps working.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(trackparse
  URL https://github.com/4matic/trackparse/releases/download/c-v0.7.0/trackparse-c-0.7.0.tar.gz
  URL_HASH SHA256=<the tarball's line in SHA256SUMS>)
FetchContent_MakeAvailable(trackparse)

target_link_libraries(app PRIVATE trackparse::trackparse)
```

`trackparse::trackparse` is a static library unless `BUILD_SHARED_LIBS` (or `TRACKPARSE_SHARED`)
is on. Under FetchContent the tests, the install rules and the warning flags of this project are
off, and the tarball builds without Node: it ships the amalgamation in `dist/`.

### The amalgamation

`trackparse.c` is the whole library in one file, sqlite style: every internal name is `static`,
so nothing but `trackparse_*` leaves the object file. Add it to your build next to the headers:

```sh
cc -std=c11 -O2 -ffp-contract=off -c trackparse.c
cc -std=c11 -O2 -I. app.c trackparse.o -lm -pthread -o app
c++ -std=c++17 -O2 -I. app.cpp trackparse.o -lm -pthread -o app   # with trackparse.hpp
```

`-ffp-contract=off` (MSVC: `/fp:precise`) keeps the compiler from fusing a multiply and an add,
which would round some edited tracklist times differently. The file also sets
`#pragma STDC FP_CONTRACT OFF` for the compilers that honour it (GCC ignores it) and rounds that
product through a `volatile`, so a build without the flag gives the same times. On Windows, define `TRACKPARSE_STATIC` in the files that include
the header, or it declares DLL imports. On 32-bit x86, compile with `-msse2 -mfpmath=sse`: the
library refuses to build with x87 arithmetic.

### From source

```sh
git clone https://github.com/4matic/trackparse
cmake -S trackparse/c -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /usr/local
```

This installs `libtrackparse`, both headers, a CMake package and a pkg-config file:

```cmake
find_package(trackparse 0.7 CONFIG REQUIRED)
target_link_libraries(app PRIVATE trackparse::trackparse)
```

```sh
cc app.c $(pkg-config --cflags --libs trackparse) -o app
```

Building from git needs Node (`scripts/amalgamate.mjs` writes the amalgamation); the release
tarball does not. `-DTRACKPARSE_SHARED=ON` builds a shared library. While the version is 0.x its
soname carries the minor version (`libtrackparse.so.0.7`): there is no ABI promise before 1.0.

## Quick start

**Docs:** [Getting started](https://4matic.github.io/trackparse/start/getting-started/)

```c
#include <stdio.h>
#include <trackparse.h>

int main(void) {
  trackparse_track *t = trackparse_parse(
      "01. Wilkinson ft. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix) [Official Video]",
      TRACKPARSE_NUL_TERMINATED);
  if (t == NULL) return 1; /* out of memory, the only failure */

  for (size_t i = 0; i < t->artists_count; i++) {
    const trackparse_artist *a = &t->artists[i];
    printf("%s (%s)\n", a->name.ptr, trackparse_role_name(a->role));
  }
  // Wilkinson (primary)
  // Becky Hill (featured)
  // Tom Cane (featured)

  printf("%s\n", t->title.ptr);      // Afterglow
  printf("%s\n", t->full_title.ptr); // Afterglow (Sub Focus VIP Remix)
  const trackparse_version *v = &t->versions[0];
  printf("%s %s %s\n", v->type_name.ptr, v->artists[0].name.ptr, v->modifiers[0].ptr);
  // remix Sub Focus vip
  printf("%s %s\n", t->junk[0].raw.ptr, t->junk[0].kind_name.ptr);        // Official Video video
  printf("%d %s\n", t->position->number, trackparse_mode_name(t->mode)); // 1 youtube

  trackparse_format_options *f = trackparse_format_options_new();
  trackparse_format_options_set_joiners(f, TRACKPARSE_JOINERS_CANONICAL);
  trackparse_string *s = trackparse_format_with(t, f);
  if (s != NULL) printf("%s\n", s->str.ptr);
  // Wilkinson feat. Becky Hill & Tom Cane - Afterglow (Sub Focus VIP Remix)

  trackparse_string_free(s);
  trackparse_format_options_free(f);
  trackparse_track_free(t);
  return 0;
}
```

The other examples below are statements from inside a `main` like this one, and they leave out
the `NULL` checks to stay short. The
[playground](https://4matic.github.io/trackparse/playground/) writes a whole C program for any
input and options you pick (choose **C** above its code panel).

## API

**Docs:** [Other functions](https://4matic.github.io/trackparse/reference/functions/) · [`trackparse.h`](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.h)

```text
trackparse_track     *trackparse_parse(s, len)                 trackparse_parse_with(s, len, options)
trackparse_artists   *trackparse_parse_artists(s, len)         trackparse_parse_artists_with(s, len, options)
trackparse_string    *trackparse_format(track)                 trackparse_format_with(track, format_options)
trackparse_string    *trackparse_normalize(s, len)
trackparse_artists   *trackparse_all_artists(track)
trackparse_parser    *trackparse_parser_new(options)           trackparse_parser_parse(parser, s, len)
                                                               trackparse_parser_parse_artists(parser, s, len)
trackparse_tracklist *trackparse_parse_tracklist(s, len)       trackparse_parse_tracklist_with(s, len, options)
trackparse_edit_result *trackparse_apply_edits(s, len, edits)
trackparse_string    *trackparse_track_to_json(track)          (and _artists_, _tracklist_, _edit_result_to_json)
const char           *trackparse_library_version(void)         trackparse_spec_version(void)
```

The header is the reference: every type, function and enum value has a comment there. The names
follow the other ports (`parse_artists`, `full_title`, `known_artist`), with the `trackparse_`
prefix on functions and types and `TRACKPARSE_` on macros and enum values.

### Strings: UTF-8 and explicit lengths

Every string argument is UTF-8 given as a pointer and a length in bytes. Pass
`TRACKPARSE_NUL_TERMINATED` as the length to have the library call `strlen`. An explicit length
lets you pass a slice of a larger buffer, and lets U+0000 through, which is real input for the
spec.

Strings in results are `trackparse_str { const char *ptr; size_t len; }`. They are also
NUL-terminated, so `printf("%s")` works for any text that has no U+0000 in it.

```c
/* An explicit length: the input may hold U+0000, or not be NUL-terminated at all. */
const char buf[] = "Noisia - Stigma (VIP)ignored";
trackparse_track *t = trackparse_parse(buf, 21);
printf("%s | %s\n", t->title.ptr, t->full_title.ptr); // Stigma | Stigma (VIP)
trackparse_track_free(t);

/* Invalid UTF-8: one U+FFFD per maximal ill-formed subpart (the WHATWG rule). */
t = trackparse_parse("Noisia - Sti\377gma\342\202", TRACKPARSE_NUL_TERMINATED);
printf("%s (%zu bytes)\n", t->title.ptr, t->title.len); // Sti�gma� (12 bytes)
trackparse_track_free(t);

const char in[] = "Noisia - Sti\0gma";
t = trackparse_parse(in, sizeof in - 1);
printf("%zu %zu\n", t->title.len, strlen(t->title.ptr)); // 7 3
trackparse_track_free(t);
```

Invalid UTF-8 is read as `TextDecoder` reads it in JavaScript and `String::from_utf8_lossy` in
Rust, so the C library, the Rust crate and the Go module read the same bytes as the same text,
and every output string is valid UTF-8.

### `trackparse_parse` and parse options

**Docs:** [parse and ParsedTrack](https://4matic.github.io/trackparse/reference/parse/) · [Options](https://4matic.github.io/trackparse/reference/options/)

`trackparse_parse` runs [rules R1 to R9](https://4matic.github.io/trackparse/reference/spec/#r1-normalization)
of the spec on one track string. `trackparse_parse_with` takes a `trackparse_parse_options`, an
opaque object built with setters. An option you never set keeps its default, and `NULL` options
are all defaults.

| Setter | Default | |
|---|---|---|
| [`_set_mode`](https://4matic.github.io/trackparse/guides/modes/) | `TRACKPARSE_MODE_AUTO` | `_CLEAN`, `_YOUTUBE`, `_FILENAME`, `_TRACKLIST`, or `_AUTO` to detect from the string. See [Modes](#modes) |
| [`_set_uploader`](https://4matic.github.io/trackparse/guides/modes/#the-uploader-fallback) | none | YouTube channel name, used as the artist when there's no separator (`- Topic` and `VEVO` are dropped) |
| [`_add_known_artist`](https://4matic.github.io/trackparse/guides/artists/#knownartists) | none | A name that is never split; call it once per name. There is no built-in list |
| [`_set_split_and`](https://4matic.github.io/trackparse/guides/artists/#and-is-special) | `TRACKPARSE_SPLIT_AND_AUTO` | `_AUTO` splits on `and` only after a comma (`A, B and C`), so `Simon and Garfunkel` stays whole. Also `_ALWAYS`, `_NEVER` |
| [`_add_version_head`](https://4matic.github.io/trackparse/guides/versions/#your-own-words), `_add_descriptor`, `_add_genre`, `_add_junk`, `_add_feat_marker` | none | Extra vocabulary ([`keywords`](https://4matic.github.io/trackparse/reference/options/#keywords)), added to the built-in lists |

Each setter's full name starts with `trackparse_parse_options`. Setters copy what they are given,
so the strings you pass can go away right after the call. They return `false` only when out of
memory. A value out of range falls back to the default, as a bad option does in every port.

```c
trackparse_parse_options *o = trackparse_parse_options_new();
trackparse_parse_options_set_mode(o, TRACKPARSE_MODE_YOUTUBE);
trackparse_parse_options_set_uploader(o, "NoisiaVEVO", TRACKPARSE_NUL_TERMINATED);
trackparse_track *t =
    trackparse_parse_with("Stigma (Official Video)", TRACKPARSE_NUL_TERMINATED, o);
printf("%s - %s\n", t->artists[0].name.ptr, t->title.ptr); // Noisia - Stigma
trackparse_track_free(t);
trackparse_parse_options_free(o);

o = trackparse_parse_options_new();
trackparse_parse_options_add_known_artist(o, "Chase & Status", TRACKPARSE_NUL_TERMINATED);
trackparse_parse_options_set_split_and(o, TRACKPARSE_SPLIT_AND_ALWAYS);
trackparse_parse_options_add_version_head(o, "rerub", TRACKPARSE_NUL_TERMINATED, "remix",
                                          TRACKPARSE_NUL_TERMINATED);
t = trackparse_parse_with("Chase & Status and Bou - Baddadan (Alix Perez Rerub)",
                          TRACKPARSE_NUL_TERMINATED, o);
for (size_t i = 0; i < t->artists_count; i++) printf("%s\n", t->artists[i].name.ptr);
// Chase & Status
// Bou
printf("%s %s\n", t->versions[0].type_name.ptr, t->versions[0].artists[0].name.ptr);
// remix Alix Perez
trackparse_track_free(t);
trackparse_parse_options_free(o);
```

The version type of `_add_version_head` and the junk kind of `_add_junk` are spec names
(`"remix"`, `"label"`), so a vocabulary can bring names of its own. A version's `type` is then
`TRACKPARSE_VERSION_VERSION` and a junk's `kind` `TRACKPARSE_JUNK_OTHER`, and `type_name` /
`kind_name` hold the name you gave. Each enum has `trackparse_<enum>_name()` for its spec name
and, for the three that options take, `_from_name()`.

### `trackparse_track`

**Docs:** [ParsedTrack fields](https://4matic.github.io/trackparse/reference/parse/#parsedtrack)

| Field | Type | |
|---|---|---|
| `input` | `trackparse_str` | The input as given (after UTF-8 repair) |
| `mode` | `trackparse_mode` | The mode used; never `_AUTO` |
| `position`, `timestamp` | pointer or `NULL` | `{raw, number}`, `{raw, seconds}` |
| `artists`, `artists_count` | `const trackparse_artist *` | `{name, role, joiner, source}`; `joiner` is `NULL` for the first |
| `title`, `full_title` | `trackparse_str` | The title, and the title with its versions |
| `versions`, `versions_count` | `const trackparse_version *` | `{type, type_name, raw, artists, modifiers, descriptor, year, unknown_artist, delimiter}` |
| `year` | `const int *` | `NULL` when there is none |
| `flags` | `trackparse_flags` | `{explicit_, clean, unknown_artist, unknown_title}` (`explicit` is a C++ keyword) |
| `junk`, `junk_count` | `const trackparse_junk *` | `{raw, kind, kind_name}` |
| `warnings`, `warnings_count` | `const trackparse_warning *` | See [Warnings](#warnings) |

Nullable fields are pointers that are `NULL` when the value is absent, as in the Go port. Arrays
are a pointer and a count.

### JSON

**Docs:** [Serializing to JSON](https://4matic.github.io/trackparse/reference/parse/#serializing-to-json)

`trackparse_track_to_json`, `trackparse_artists_to_json`, `trackparse_tracklist_to_json` and
`trackparse_edit_result_to_json` return the bytes `JSON.stringify` writes for the same result in
JavaScript, with the spec's camelCase keys:

```c
trackparse_track *t = trackparse_parse(
    "Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]", TRACKPARSE_NUL_TERMINATED);
trackparse_string *json = trackparse_track_to_json(t);
if (json != NULL) printf("%s\n", json->str.ptr);
trackparse_string_free(json);
trackparse_track_free(t);
```

<details>
<summary>Output</summary>

```json
{"input":"Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]","mode":"youtube","position":null,"timestamp":null,"artists":[{"name":"Wilkinson","role":"primary","joiner":null,"source":"artist"},{"name":"Becky Hill","role":"featured","joiner":"ft.","source":"artist"}],"title":"Afterglow","fullTitle":"Afterglow (Sub Focus VIP Remix)","versions":[{"type":"remix","raw":"Sub Focus VIP Remix","artists":[{"name":"Sub Focus","role":"remixer","joiner":null,"source":"version"}],"modifiers":["vip"],"descriptor":null,"year":null,"unknownArtist":false,"delimiter":"("}],"year":null,"flags":{"explicit":false,"clean":false,"unknownArtist":false,"unknownTitle":false},"junk":[{"raw":"HD","kind":"quality"}],"warnings":[]}
```

</details>

The library writes JSON but does not read it; there is no `from_json`.

### `trackparse_parse_artists`, `trackparse_normalize`, `trackparse_all_artists`

**Docs:** [parseArtists](https://4matic.github.io/trackparse/reference/functions/#parseartistsinput-options) · [normalize](https://4matic.github.io/trackparse/reference/functions/#normalizeinput) · [allArtists](https://4matic.github.io/trackparse/reference/functions/#allartiststrack)

```c
trackparse_artists *list =
    trackparse_parse_artists("Camo & Krooked feat. Ayah Marar", TRACKPARSE_NUL_TERMINATED);
for (size_t i = 0; i < list->count; i++)
  printf("%s %s\n", list->items[i].name.ptr, trackparse_role_name(list->items[i].role));
// Camo primary
// Krooked primary
// Ayah Marar featured
trackparse_artists_free(list);

trackparse_string *n = trackparse_normalize("Noisia  –  Stigma", TRACKPARSE_NUL_TERMINATED);
printf("%s\n", n->str.ptr); // Noisia - Stigma
trackparse_string_free(n);

trackparse_track *t =
    trackparse_parse("Noisia - Stigma (Phace & Misanthrop Remix)", TRACKPARSE_NUL_TERMINATED);
trackparse_artists *all = trackparse_all_artists(t);
for (size_t i = 0; i < all->count; i++)
  printf("%s %s\n", all->items[i].name.ptr, trackparse_source_name(all->items[i].source));
// Noisia artist
// Phace version
// Misanthrop version
trackparse_artists_free(all);
trackparse_track_free(t);

printf("%d\n", strcmp(TRACKPARSE_VERSION, trackparse_library_version()) == 0); // 1
```

[`trackparse_normalize`](https://4matic.github.io/trackparse/reference/spec/#r1-normalization) is
the Unicode cleanup every function runs first. `TRACKPARSE_VERSION` is the version of the header,
`trackparse_library_version()` that of the linked library (they differ when a shared library was
replaced), and `trackparse_spec_version()` the spec version, which is the same number: all ports
and the spec are released together.

### `trackparse_format`

**Docs:** [format](https://4matic.github.io/trackparse/reference/functions/#formattrack-options) · [Formatting](https://4matic.github.io/trackparse/guides/formatting/)

Writes a parsed track back as `Artists - Title (Versions)`. `trackparse_format_options` has
`_set_feat` (`TRACKPARSE_FEAT_SOURCE`, `_ARTIST`, `_TITLE`, `_OMIT`), `_set_feat_marker` (`""` is
a valid marker; unset keeps the input's), `_set_joiners` (`TRACKPARSE_JOINERS_ORIGINAL`,
`_CANONICAL`), `_set_versions` (`TRACKPARSE_VERSIONS_ALL`, `_NONE`), `_set_omit_producers` and
`_set_position`. The [quick start](#quick-start) has an example.

### `trackparse_parser`

**Docs:** [createParser](https://4matic.github.io/trackparse/reference/functions/#createparseroptions)

`trackparse_parse_with` builds the word tables for its options on every call. A parser builds them
once. It copies the options, so they can be freed right away, and it never changes afterwards.

```c
trackparse_parse_options *o = trackparse_parse_options_new();
trackparse_parse_options_add_known_artist(o, "Camo & Krooked", TRACKPARSE_NUL_TERMINATED);
trackparse_parser *p = trackparse_parser_new(o);
trackparse_parse_options_free(o); /* the parser keeps a copy */

const char *inputs[] = {"Camo & Krooked - Kallisto", "Camo & Krooked x Mefjus - Ember"};
for (size_t i = 0; i < 2; i++) {
  trackparse_track *t = trackparse_parser_parse(p, inputs[i], TRACKPARSE_NUL_TERMINATED);
  printf("%zu artists, %s\n", t->artists_count, t->artists[0].name.ptr);
  trackparse_track_free(t);
}
// 1 artists, Camo & Krooked
// 2 artists, Camo & Krooked
trackparse_parser_free(p);
```

## Memory ownership

Every result is one block that owns everything it points to: strings, arrays, nested tracks.
Free it with the `_free` function of its type, and nothing else:

| Returned by | Free with |
|---|---|
| `trackparse_parse`, `_parse_with`, `_parser_parse` | `trackparse_track_free` |
| `trackparse_parse_artists`, `_parse_artists_with`, `_parser_parse_artists`, `_all_artists` | `trackparse_artists_free` |
| `trackparse_format`, `_format_with`, `_normalize`, every `_to_json` | `trackparse_string_free` |
| `trackparse_parse_tracklist`, `_parse_tracklist_with` | `trackparse_tracklist_free` |
| `trackparse_apply_edits` | `trackparse_edit_result_free` |
| `trackparse_parser_new` | `trackparse_parser_free` |
| `trackparse_*_options_new`, `trackparse_edits_new` | the matching `_free` |

Results never point into each other or into your input, so they can be freed in any order:

```c
trackparse_track *t = trackparse_parse("Noisia - Stigma (2015)", TRACKPARSE_NUL_TERMINATED);
trackparse_string *s = trackparse_format(t);
trackparse_track_free(t); /* s has its own memory: it stays valid */
printf("%s\n", s->str.ptr); // Noisia - Stigma
trackparse_string_free(s);
trackparse_track_free(NULL); /* every _free accepts NULL */
```

Result structs are read-only. Only the library allocates them, so a later version can append
fields without breaking programs built against an earlier one.

## Thread safety

Every function except `trackparse_set_allocator` can be called from any number of threads at
once. A `trackparse_parser` is
immutable once created and can be shared between threads without a lock. Options and edit lists
are plain objects: don't change one while another thread reads it.

The default word tables are built on first use, behind a lock, and kept for the life of the
process. A leak checker lists them as still reachable, not as leaks.

## Out of memory

Running out of memory is the only way a call can fail. A function that returns a pointer returns
`NULL`, a setter returns `false` and leaves the option unchanged, and nothing leaks: the work is
done in arenas that are freed as a whole. A test fails the n-th allocation for every n that a set
of inputs reaches and checks exactly that.

`trackparse_set_allocator` replaces `malloc`, `realloc` and `free` for the whole process. Call it
once, before any other function:

```c
#include <stdio.h>
#include <stdlib.h>
#include <trackparse.h>

static size_t live; /* blocks currently allocated */

static void *my_malloc(size_t size, void *user) {
  (void)user;
  void *p = malloc(size);
  if (p != NULL) live++;
  return p;
}
static void my_free(void *p, void *user) {
  (void)user;
  if (p != NULL) live--;
  free(p);
}

int main(void) {
  /* Once, before any other call. realloc_fn may be NULL. */
  trackparse_allocator a = {my_malloc, NULL, my_free, NULL};
  trackparse_set_allocator(&a);

  /* The first call builds the default word tables, which stay for the life of the process. */
  trackparse_track_free(trackparse_parse("", 0));
  size_t before = live;

  trackparse_track *t = trackparse_parse("Noisia - Stigma", TRACKPARSE_NUL_TERMINATED);
  printf("%zu\n", live - before); // 1: a result is one block
  trackparse_track_free(t);
  printf("%zu\n", live - before); // 0
  return 0;
}
```

## Modes

**Docs:** [Modes](https://4matic.github.io/trackparse/guides/modes/) · [R2 in the spec](https://4matic.github.io/trackparse/reference/spec/#r2-mode-prelude)

| Mode | For | Adds |
|---|---|---|
| `TRACKPARSE_MODE_CLEAN` | Store metadata, tags | Only a spaced dash separates artist and title |
| [`TRACKPARSE_MODE_YOUTUBE`](https://4matic.github.io/trackparse/guides/modes/#youtube-mode) | Video titles, scrobbles | Pipe segments, `"Title" by Artist`, `Artist: "Title"`, unspaced dashes, trailing junk, uploader fallback |
| [`TRACKPARSE_MODE_FILENAME`](https://4matic.github.io/trackparse/guides/modes/#filename-mode) | Files | Strips the extension, `_` becomes a space, `01-Track` numbering |
| [`TRACKPARSE_MODE_AUTO`](https://4matic.github.io/trackparse/guides/modes/#how-auto-decides) | Anything | Filename for known extensions, YouTube when it sees junk, pipes or platform suffixes, clean otherwise |
| [`TRACKPARSE_MODE_TRACKLIST`](https://4matic.github.io/trackparse/guides/modes/#tracklist-mode) | One line of a tracklist | The first spaced dash always separates; `_-_` too. Never picked by auto |

```c
trackparse_parse_options *youtube = trackparse_parse_options_new();
trackparse_parse_options_set_mode(youtube, TRACKPARSE_MODE_YOUTUBE);
trackparse_track *t =
    trackparse_parse_with("\"Numb\" by Linkin Park", TRACKPARSE_NUL_TERMINATED, youtube);
printf("%s / %s %s\n", t->artists[0].name.ptr, t->title.ptr,
       trackparse_warning_name(t->warnings[0]));
// Linkin Park / Numb bySplit
trackparse_track_free(t);

t = trackparse_parse("07_Burial_-_Archangel.mp3", TRACKPARSE_NUL_TERMINATED);
printf("%s %s %s %d\n", trackparse_mode_name(t->mode), t->artists[0].name.ptr, t->title.ptr,
       t->position->number);
// filename Burial Archangel 7
trackparse_track_free(t);

trackparse_parse_options_set_uploader(youtube, "Porter Robinson", TRACKPARSE_NUL_TERMINATED);
t = trackparse_parse_with("Shelter", TRACKPARSE_NUL_TERMINATED, youtube);
printf("%s\n", t->artists[0].name.ptr); // Porter Robinson
trackparse_track_free(t);
trackparse_parse_options_free(youtube);
```

## Warnings

**Docs:** [Warnings](https://4matic.github.io/trackparse/reference/warnings/)

The parser never fails, but it says when it had to guess. `warnings` is empty when every decision
was unambiguous. The values are `TRACKPARSE_WARNING_NO_SEPARATOR`, `_AMBIGUOUS_SEPARATOR`,
`_UNSPACED_DASH_SPLIT`, `_ASYMMETRIC_DASH_SPLIT`, `_BY_SPLIT`, `_QUOTED_TITLE_SPLIT`,
`_AMBIGUOUS_MIX_CREDIT` and `_UNBALANCED_BRACKETS`; `trackparse_warning_name` gives the JSON
name, and the [warnings reference](https://4matic.github.io/trackparse/reference/warnings/#track-warnings)
explains each one.

```c
trackparse_track *t = trackparse_parse("Around the World", TRACKPARSE_NUL_TERMINATED);
printf("%zu %s %s\n", t->artists_count, t->title.ptr, trackparse_warning_name(t->warnings[0]));
// 0 Around the World noSeparator
printf("%d\n", t->warnings[0] == TRACKPARSE_WARNING_NO_SEPARATOR); // 1
trackparse_track_free(t);

t = trackparse_parse("Noisia - Stigma (VIP", TRACKPARSE_NUL_TERMINATED);
printf("%s\n", trackparse_warning_name(t->warnings[0])); // unbalancedBrackets
trackparse_track_free(t);
```

## Tracklists and edits

**Docs:** [Tracklists](https://4matic.github.io/trackparse/guides/tracklists/) · [Editing tracklists](https://4matic.github.io/trackparse/guides/editing-tracklists/) · [Tracklist API](https://4matic.github.io/trackparse/reference/tracklist/) · [Warnings](https://4matic.github.io/trackparse/reference/warnings/#tracklist-warnings)

`trackparse_parse_tracklist` parses a whole tracklist, one entry per line, and checks it. Each
entry keeps its line exactly as written plus its own line ending (`eol`), so the entries joined
give back the input, CRLF or not.

```c
const char *text = "Hour 2\r\n"
                   "48:20 Noisia - Stigma\r\n"
                   "52:47 Mefjus & Kasra - Bandwagon (Phace Remix)\r\n"
                   "56:05 Black Sun Empire - ID\r\n"
                   "59:31 Alix Perez - Fade Away\r\n"
                   "01:03:10 Interview with Hybris\r\n"
                   "01:07:44 Imanu - Sacrifice\r\n"
                   "00:11:02 Current Value feat. Teddy Killerz - Payload\r\n" /* 01:11:02 */
                   "01:15:36 Hybris - Spinning Top";

trackparse_tracklist *list = trackparse_parse_tracklist(text, TRACKPARSE_NUL_TERMINATED);
if (list == NULL) return 1;

for (size_t i = 0; i < list->entries_count; i++) {
  const trackparse_entry *e = &list->entries[i];
  printf("%d %s%s%s\n", e->line, trackparse_entry_kind_name(e->kind), e->time ? " " : "",
         e->time ? e->time->raw.ptr : "");
}
// 1 header
// 2 track 48:20
// 3 track 52:47
// 4 track 56:05
// 5 track 59:31
// 6 marker 01:03:10
// 7 track 01:07:44
// 8 track 00:11:02
// 9 track 01:15:36

const trackparse_entry *e = &list->entries[2];
printf("%g %s | %s\n", e->time->seconds, e->parts.rest.ptr, e->track->title.ptr);
// 3167 Mefjus & Kasra - Bandwagon (Phace Remix) | Bandwagon
printf("%d\n", list->entries[3].track->flags.unknown_title); // 1 (the ID)

for (size_t i = 0; i < list->warnings_count; i++) {
  const trackparse_tracklist_warning *w = &list->warnings[i];
  printf("%s %d %s\n", trackparse_tracklist_warning_code_name(w->code), w->line,
         trackparse_severity_name(w->severity));
  if (w->suggestion)
    printf("  suggestion: %d %s %g\n", w->suggestion->line, w->suggestion->raw.ptr,
           w->suggestion->seconds);
}
// no-start-at-zero 2 info
// time-out-of-order 8 warning
//   suggestion: 8 01:11:02 4262

const trackparse_stats *s = &list->stats;
printf("%d %d %d %d %g %g\n", s->lines, s->tracks, s->markers, s->timed, *s->first_time,
       *s->last_time);
// 9 7 1 8 2900 4536
trackparse_tracklist_free(list);
```

The [suggestion](https://4matic.github.io/trackparse/reference/warnings/#suggestions) exists
because exactly one time, one digit away from `00:11:02`, fits between its neighbours. With
several candidates or none, `suggestion` is `NULL`.
[Open this list in the playground](https://4matic.github.io/trackparse/playground/#list=Hour%202%0A48%3A20%20Noisia%20-%20Stigma%0A52%3A47%20Mefjus%20%26%20Kasra%20-%20Bandwagon%20%28Phace%20Remix%29%0A56%3A05%20Black%20Sun%20Empire%20-%20ID%0A59%3A31%20Alix%20Perez%20-%20Fade%20Away%0A01%3A03%3A10%20Interview%20with%20Hybris%0A01%3A07%3A44%20Imanu%20-%20Sacrifice%0A00%3A11%3A02%20Current%20Value%20feat.%20Teddy%20Killerz%20-%20Payload%0A01%3A15%3A36%20Hybris%20-%20Spinning%20Top).

`trackparse_apply_edits` takes an edit list and rewrites only the lines the edits name. Build the
list with `trackparse_edits_set_time(e, line, seconds)`, `_shift(e, seconds)` (every timed
line), `_shift_from(e, seconds, from)`, `_shift_lines(e, seconds, lines, count)`,
`_sort_by_time(e)`, `_set_rest(e, line, rest, len)`, `_insert(e, after, text, len)` (`after` 0
inserts at the top) and `_delete(e, line)`. Lines are 1-based, and the edits apply in order.

```c
const char *text = "48:20 Noisia - Stigma\r\n"
                   "59:31 Alix Perez - Fade Away\r\n"
                   "00:03:10 Imanu - Sacrifice\r\n";

trackparse_edits *edits = trackparse_edits_new();
trackparse_edits_set_time(edits, 3, 3790);
trackparse_edits_shift_from(edits, 45, 2);
trackparse_edit_result *r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, edits);
trackparse_edits_free(edits);

fputs(r->text.ptr, stdout);
// 48:20 Noisia - Stigma
// 01:00:16 Alix Perez - Fade Away
// 01:03:55 Imanu - Sacrifice
for (size_t i = 0; i < r->changed_count; i++) printf("changed %lld\n", r->changed[i]);
// changed 2
// changed 3
const trackparse_edit_warning *w = &r->warnings[0];
printf("%s %zu %lld\n", trackparse_edit_warning_code_name(w->code), w->edit, *w->line);
// style-upgraded 1 2
trackparse_edit_result_free(r);

edits = trackparse_edits_new();
trackparse_edits_delete(edits, 9);
r = trackparse_apply_edits(text, TRACKPARSE_NUL_TERMINATED, edits);
printf("%s %d\n", trackparse_edit_warning_code_name(r->warnings[0].code),
       strcmp(r->text.ptr, text) == 0);
// edit-invalid 1
trackparse_edit_result_free(r);
trackparse_edits_free(edits);
```

A rewritten time keeps its line's style: `48:20` stays `MM:SS`, and a time that crosses the hour
gets hours, with a `style-upgraded` warning. A time pushed below zero stops at zero and warns
`time-clamped`. A bad edit changes nothing and warns `edit-invalid`
([edit warnings](https://4matic.github.io/trackparse/reference/warnings/#edit-warnings)).

`trackparse_tracklist_options` ([options](https://4matic.github.io/trackparse/reference/options/#tracklist-options))
has the parse options' setters (`_set_mode` defaults to `TRACKPARSE_MODE_TRACKLIST` here), plus
`_set_audio_seconds` (the length of the mix; it enables `time-after-audio-end`, and 0 is a valid
length) and `_add_marker_word`:

```c
trackparse_tracklist_options *o = trackparse_tracklist_options_new();
trackparse_tracklist_options_set_audio_seconds(o, 3600);
trackparse_tracklist *list =
    trackparse_parse_tracklist_with("00:00 A - B\n1:02:03 C - D", TRACKPARSE_NUL_TERMINATED, o);
printf("%s %d\n", trackparse_tracklist_warning_code_name(list->warnings[0].code),
       list->warnings[0].line);
// time-after-audio-end 2
trackparse_tracklist_free(list);
trackparse_tracklist_options_free(o);
```

| Result | Fields |
|---|---|
| `trackparse_tracklist` | `spec_version`, `entries`, `warnings`, `stats` |
| `trackparse_entry` | `line` (1-based), `kind` (`TRACKPARSE_ENTRY_TRACK`, `_MARKER`, `_HEADER`, `_LINK`, `_NOTE`, `_BLANK`), `eol`, `raw`, `parts`, `time` (or `NULL`), `track` (or `NULL`), `layered` |
| `trackparse_parts` | `lead`, `time`, `gap`, `rest`: joined they give `raw` |
| `trackparse_tracklist_warning` | `code`, `line`, `severity` (`TRACKPARSE_SEVERITY_INFO` or `_WARNING`), `suggestion` (or `NULL`) |
| `trackparse_stats` | `lines`, `tracks`, `markers`, `timed`, `first_time` and `last_time` (`const double *`) |
| `trackparse_edit_result` | `text`, `changed` (`long long` line numbers), `warnings` (`{code, edit, line}`; `line` is `const long long *`) |

Times are `double` seconds: a time may have a fraction (`12:34.5`). Tracklist warning codes are
`TRACKPARSE_TL_TIME_MISSING` to `TRACKPARSE_TL_LINE_UNPARSED`, edit warnings
`TRACKPARSE_EDIT_WARNING_EDIT_INVALID`, `_TIME_CLAMPED` and `_STYLE_UPGRADED`. The rules are in
[R13 of the spec](https://4matic.github.io/trackparse/reference/spec/#r13-tracklists).

## C++ wrapper

**Docs:** [Other functions](https://4matic.github.io/trackparse/reference/functions/) · [`trackparse.hpp`](https://github.com/4matic/trackparse/blob/main/c/include/trackparse.hpp)

`trackparse.hpp` is a header-only C++17 layer over the same library: no second implementation,
the same output. It is installed and shipped next to `trackparse.h`, and links against the same
`trackparse::trackparse` target.

- Results (`track`, `artists`, `tracklist`, `edit_result`), `parser` and the option builders are
  move-only owners that free their C object in the destructor.
- Strings are `std::string_view`, nullable fields `std::optional`, arrays ranges with `begin`,
  `end`, `size` and `operator[]`. Views borrow from their owner and are valid while it lives.
- Option builders chain: `parse_options{}.mode(mode::youtube).known_artist("Chase & Status")`.
- The enums are `enum class` (`trackparse::role::featured`), and `trackparse::name(value)` gives
  the spec name.
- The only exception is `std::bad_alloc`, when the library runs out of memory. Built with
  `-fno-exceptions`, the wrapper calls `std::abort()` instead.

```cpp
#include <iostream>
#include <trackparse.hpp>

int main() {
  auto t = trackparse::parse("Wilkinson ft. Becky Hill - Afterglow (Sub Focus VIP Remix) [HD]");
  for (const auto &a : t.artists()) {
    std::cout << a.name() << " (" << trackparse::name(a.role()) << ")\n";
  }
  // Wilkinson (primary)
  // Becky Hill (featured)
  std::cout << t.title() << '\n'; // Afterglow
  auto v = t.versions()[0];
  std::cout << v.type_name() << ' ' << v.artists()[0].name() << '\n'; // remix Sub Focus
  std::cout << t.year().has_value() << '\n';                          // 0
  std::cout << t.format(trackparse::format_options{}.feat(trackparse::feat::title)) << '\n';
  // Wilkinson - Afterglow (ft. Becky Hill) (Sub Focus VIP Remix)

  // Options chain. A parser keeps them for many calls and can be shared between threads.
  trackparse::parser p(trackparse::parse_options{}
                           .mode(trackparse::mode::youtube)
                           .known_artists({"Chase & Status", "Camo & Krooked"}));
  auto t2 = p.parse("Chase & Status x Camo & Krooked - Track [Official Video]");
  std::cout << t2.artists().size() << ' ' << t2.junk()[0].kind_name() << '\n'; // 2 video

  // Tracklists and edits.
  std::string text = "0:00 Noisia - Stigma\n4:20 Mefjus - Sinkhole\n9:45 Phace - Psycho";
  auto list = trackparse::parse_tracklist(text, trackparse::tracklist_options{}.audio_seconds(500));
  for (const auto &w : list.warnings()) {
    std::cout << trackparse::name(w.code()) << " on line " << w.line() << '\n';
  }
  // time-after-audio-end on line 3
  auto r = trackparse::apply_edits(text, trackparse::edits{}.shift_from(30, 2));
  std::cout << r.text() << '\n';
  // 0:00 Noisia - Stigma
  // 4:50 Mefjus - Sinkhole
  // 10:15 Phace - Psycho

  std::cout << trackparse::parse_artists("Camo & Krooked feat. Ayah Marar").to_json() << '\n';
  // [{"name":"Camo","role":"primary","joiner":null,"source":"artist"},{"name":"Krooked",...
}
```

Every owner has `c_ptr()` for the C object underneath, and `track::release()` hands the C result
over to code that frees it with `trackparse_track_free`. The wrapper is tested with GCC, Clang and
MSVC in C++17 and C++20.

## Building and testing

**Docs:** [How it's tested](https://4matic.github.io/trackparse/project/testing/) · [Ports](https://4matic.github.io/trackparse/project/ports/)

From `c/` in the [repository](https://github.com/4matic/trackparse) (CMake 3.16 or later; Node
for the codegen scripts, the amalgamation and the schema check):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
TRACKPARSE_REPORT=1 ./build/trackparse-fixtures ../spec/fixtures/*/*.json   # pass rate per file and rule
node scripts/gen_data.mjs --check        # spec/data codegen is up to date
node scripts/gen_unicode.mjs --check     # Unicode tables are up to date
```

What `ctest` runs:

- `fixtures`: all 13,130 shared fixtures (1,203 written by hand, the rest generated from the
  rules), with an empty skip list. `schema` then checks every output against the JSON Schema with
  the validator of the JavaScript package.
- `match`, `rt`, `api`: the fixture matcher and the JSON reader, the internal runtime (arenas,
  strings, vectors, the stable sort), and the public C API, ownership and `NULL` handling.
- `primitives`, `unicode`, `num`: the R0 primitives, the Unicode tables (letters, uppercase, the
  lowercase mapping, NFC and NFD) and the number code (decimal to double, JavaScript's number
  formatting, half-up rounding), each against values dumped from Node.
- `properties`: the spec's invariants on seeded random input, among them that no word of the
  input goes missing, and the tracklist round trips of R13.11, with time ceilings for 10,000 code
  points and 10,000 lines.
- `threads`: 16 threads against a sequential run. `locale`: every output after
  `setlocale(LC_ALL, "")` and under `de_DE.UTF-8` and `tr_TR.UTF-8` (when installed) equals the
  `C` locale's. `oom`: the n-th allocation fails, for every n, on about 50 inputs across the API.
- `cpp17`, `cpp20`, `example_*`, `exports`: the C++ wrapper, the programs in
  [`examples/`](https://github.com/4matic/trackparse/tree/main/c/examples) against their expected
  output, and a check that the library exports only `trackparse_*`. Tests labelled `consumer`
  install the library and build the examples with `find_package`, pkg-config, the amalgamation and
  FetchContent.

Sanitizers and fuzzing are CMake options:

```sh
cmake -S . -B build-asan -DTRACKPARSE_SANITIZE=ON          # AddressSanitizer + UBSan
cmake -S . -B build-tsan -DTRACKPARSE_SANITIZE_THREAD=ON   # ThreadSanitizer
CC=clang CXX=clang++ cmake -S . -B build-msan -DTRACKPARSE_SANITIZE_MEMORY=ON
cmake --build build-msan --target trackparse-fixtures test_api test_properties test_oom
CC=clang CXX=clang++ cmake -S . -B build-fuzz -DTRACKPARSE_FUZZ=ON -DTRACKPARSE_BUILD_TESTS=OFF
cmake --build build-fuzz                                    # libFuzzer targets fuzz_*
node scripts/fuzz_corpus.mjs corpus                         # seed corpus from the fixture inputs
./build-fuzz/fuzz_parse -max_total_time=60 corpus/parse
```

Under a sanitizer the property tests run a quarter of their cases and the time ceilings are ten
times wider. MemorySanitizer builds only the C programs: the C++ tests would need an instrumented
libc++. There is a libFuzzer target for each of `parse`, `parse_artists`, `format`,
`parse_tracklist` and `apply_edits` (`fuzz_parse` and so on) and one for the JSON reader the tests
use (`fuzz_wire`).

CI ([`c.yml`](https://github.com/4matic/trackparse/blob/main/.github/workflows/c.yml)) builds and
tests with GCC and Clang on Linux x86-64, GCC on ARM64 and on 32-bit x86, GCC 9 with CMake 3.16, a
shared-library build, Apple Clang on macOS and MSVC on Windows. It also runs the suite under
ASan + UBSan (GCC and Clang), ThreadSanitizer, MemorySanitizer and Valgrind, under German and
Turkish locales, and fuzzes every target for ten minutes once a week.
[`scripts/compare-ports.mjs`](https://github.com/4matic/trackparse/blob/main/scripts/compare-ports.mjs)
runs every fixture input through the JavaScript package and this library (built as
`trackparse-dump`) and compares the complete JSON output, every field of it, not only the ones a
fixture asserts.

Files mirror the JavaScript reference one to one (`src/scanner.c` is `scanner.ts`, `src/tl_parse.c`
is `tracklist/parse.ts`, and so on) and cite the same spec rule IDs. Behaviour changes start in
the spec, not here: see [AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md).

## Performance

There are no regular expressions, so there is no regex backtracking. On an Intel i7-4770S with
GCC 9 (a CMake Release build, `-O3`), with other programs running: 1 to 6 ms for 10,000 code points
of one repeated block (the blocks of the other ports' time ceilings), and 0.34 to 0.39 s for a
10,000-line tracklist (`build/test_bench` prints these). The Go port takes 3 to 7 ms and 0.33 to 0.38 s for the same
kinds of input on the same machine.

## Unicode version

The Unicode facts the rules need (letters, uppercase letters, the lowercase mapping, and the NFC
and NFD tables) are generated from Node 24, which has Unicode 17, and compiled into the library,
as in the Go port. The C library, the JavaScript package and the Go module give the same bytes
for any input. The Rust crate takes NFC from `unicode-normalization` (Unicode 18), so 32 newer
combining marks can come out in another order there
([what the Rust port taught](https://4matic.github.io/trackparse/project/ports/#what-the-rust-port-taught)).

## Versioning

**Changelog:** [c/CHANGELOG.md](https://github.com/4matic/trackparse/blob/main/c/CHANGELOG.md)

The library version is the spec version. All five ports and the spec are released together and
share one version number; C releases are tagged `c-vX.Y.Z`. While it is 0.x, a release that
changes the output for some input bumps the minor version, and a patch release only fixes bugs.
The shared library makes no ABI promise before 1.0.

## Other languages

**Docs:** [Ports](https://4matic.github.io/trackparse/project/ports/)

| Language | Package | Install | Docs |
|---|---|---|---|
| JavaScript / TypeScript | [trackparse on npm](https://www.npmjs.com/package/trackparse) | `npm install trackparse` | [README](https://github.com/4matic/trackparse/blob/main/js/README.md) · [API](https://4matic.github.io/trackparse/reference/parse/) |
| Python | [trackparse on PyPI](https://pypi.org/project/trackparse/) | `pip install trackparse` | [README](https://github.com/4matic/trackparse/blob/main/python/README.md) · [API](https://4matic.github.io/trackparse/reference/parse/) |
| Rust | [trackparse on crates.io](https://crates.io/crates/trackparse) | `cargo add trackparse` | [README](https://github.com/4matic/trackparse/blob/main/rust/README.md) · [API on docs.rs](https://docs.rs/trackparse) |
| Go | [github.com/4matic/trackparse/go](https://pkg.go.dev/github.com/4matic/trackparse/go) | `go get github.com/4matic/trackparse/go` | [README](https://github.com/4matic/trackparse/blob/main/go/README.md) · [API on pkg.go.dev](https://pkg.go.dev/github.com/4matic/trackparse/go) |

All of them implement one spec, pass the same fixtures and return identical JSON for every
fixture input, so you can parse in one language and read the result in another.

## Limitations

**Docs:** [Limitations and roadmap](https://4matic.github.io/trackparse/project/roadmap/)

- `trackparse_parse` takes one track string. Whole tracklists go through
  `trackparse_parse_tracklist`, which reads start times only: no durations, trailing times or
  time ranges.
- Catalog numbers, key and BPM are not fields. They stay in the title.
- There is no artist database. Add known artists for names that contain a joiner
  (`Earth, Wind & Fire`).
- No package manager carries the library yet: use FetchContent, the amalgamation or an install
  from source.

## License

[MIT](https://github.com/4matic/trackparse/blob/main/c/LICENSE)
