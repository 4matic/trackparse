---
title: What it understands
description: Real inputs and what trackparse returns for them, from feats and remixes to file names and YouTube junk.
---

Every row below is a real result of `parse(input)` with default options, except the two marked
*youtube*. Click an input to open it in the [playground](/trackparse/playground/) and see the full
output.

The results are the same in JavaScript, Python, Rust, Go and C, because all five follow one
[spec](/trackparse/reference/spec/). The values are shown as they appear in JSON: `remix`, `bySplit`.
In Rust they are enum variants such as [`VersionType::Remix`](https://docs.rs/trackparse/latest/trackparse/enum.VersionType.html) and
[`Warning::BySplit`](https://docs.rs/trackparse/latest/trackparse/enum.Warning.html). In Go they are typed string constants such as
[`trackparse.VersionTypeRemix`](https://pkg.go.dev/github.com/4matic/trackparse/go#VersionTypeRemix) and
[`trackparse.WarningBySplit`](https://pkg.go.dev/github.com/4matic/trackparse/go#WarningBySplit), whose values are these same names. In C they are enum values such as `TRACKPARSE_VERSION_REMIX`
and `TRACKPARSE_WARNING_BY_SPLIT`, and functions such as `trackparse_version_type_name` return
these names. The columns
are fields of [`ParsedTrack`](/trackparse/reference/parse/#parsedtrack).

| Input | Artists | Title | Versions and other fields |
|---|---|---|---|
| [`Noisia feat. Foreign Beggars - Shellshock`](/trackparse/playground/#track=Noisia%20feat.%20Foreign%20Beggars%20-%20Shellshock) | Noisia, Foreign Beggars *(feat)* | `Shellshock` |  |
| [`Silk City - Electricity (with Dua Lipa)`](/trackparse/playground/#track=Silk%20City%20-%20Electricity%20%28with%20Dua%20Lipa%29) | Silk City, Dua Lipa *(feat)* | `Electricity` |  |
| [`Skrillex & Diplo x Justin Bieber - Where Are Ü Now`](/trackparse/playground/#track=Skrillex%20%26%20Diplo%20x%20Justin%20Bieber%20-%20Where%20Are%20%C3%9C%20Now) | Skrillex, Diplo, Justin Bieber | `Where Are Ü Now` |  |
| [`Future - Mask Off (prod. by Metro Boomin)`](/trackparse/playground/#track=Future%20-%20Mask%20Off%20%28prod.%20by%20Metro%20Boomin%29) | Future, Metro Boomin *(producer)* | `Mask Off` |  |
| [`Linkin Park - Numb (Remix by Skrillex)`](/trackparse/playground/#track=Linkin%20Park%20-%20Numb%20%28Remix%20by%20Skrillex%29) | Linkin Park | `Numb` | remix by Skrillex |
| [`Dimension - Offshore (Mefjus Remix / Noisia Remix)`](/trackparse/playground/#track=Dimension%20-%20Offshore%20%28Mefjus%20Remix%20%2F%20Noisia%20Remix%29) | Dimension | `Offshore` | two remixes |
| [`Pendulum - Watercolour (Extended VIP Mix)`](/trackparse/playground/#track=Pendulum%20-%20Watercolour%20%28Extended%20VIP%20Mix%29) | Pendulum | `Watercolour` | vip, +extended |
| [`Noisia - Stigma (Phace Bootleg)`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%28Phace%20Bootleg%29) | Noisia | `Stigma` | bootleg by Phace |
| [`Deadmau5 - Strobe (Radio Edit)`](/trackparse/playground/#track=Deadmau5%20-%20Strobe%20%28Radio%20Edit%29) | Deadmau5 | `Strobe` | radio |
| [`Netsky - Iron Heart (Levela Mix)`](/trackparse/playground/#track=Netsky%20-%20Iron%20Heart%20%28Levela%20Mix%29) | Netsky | `Iron Heart` | remix by Levela |
| [`The Beatles - Hey Jude - Remastered 2015`](/trackparse/playground/#track=The%20Beatles%20-%20Hey%20Jude%20-%20Remastered%202015) | The Beatles | `Hey Jude` | remaster, 2015 |
| [`Taylor Swift - Love Story (Taylor's Version)`](/trackparse/playground/#track=Taylor%20Swift%20-%20Love%20Story%20%28Taylor%27s%20Version%29) | Taylor Swift | `Love Story` | version, "Taylor's" |
| [`Kordhell - Murder In My Mind (Slowed + Reverb)`](/trackparse/playground/#track=Kordhell%20-%20Murder%20In%20My%20Mind%20%28Slowed%20%2B%20Reverb%29) | Kordhell | `Murder In My Mind` | slowed |
| [`Eminem - Without Me (Explicit)`](/trackparse/playground/#track=Eminem%20-%20Without%20Me%20%28Explicit%29) | Eminem | `Without Me` | explicit flag |
| [`Daft Punk - One More Time (Official Music Video) [HD]`](/trackparse/playground/#track=Daft%20Punk%20-%20One%20More%20Time%20%28Official%20Music%20Video%29%20%5BHD%5D) | Daft Punk | `One More Time` | junk: video, quality |
| [`Elektronomia - Sky High [NCS Release]`](/trackparse/playground/#track=Elektronomia%20-%20Sky%20High%20%5BNCS%20Release%5D) | Elektronomia | `Sky High` | junk: label |
| [`Noisia - concussion • VISION • VISION RADIO RELOAD`](/trackparse/playground/#track=Noisia%20-%20concussion%20%E2%80%A2%20VISION%20%E2%80%A2%20VISION%20RADIO%20RELOAD) | Noisia | `concussion` | [label](#labels-after-a-bullet) `VISION`, junk: note |
| [`Porter Robinson - Shelter - YouTube`](/trackparse/playground/#track=Porter%20Robinson%20-%20Shelter%20-%20YouTube) | Porter Robinson | `Shelter` | junk: platform |
| [`01. Aphex Twin - Xtal`](/trackparse/playground/#track=01.%20Aphex%20Twin%20-%20Xtal) | Aphex Twin | `Xtal` | position 1 |
| [`[12:34] Noisia - Stigma`](/trackparse/playground/#track=%5B12%3A34%5D%20Noisia%20-%20Stigma) | Noisia | `Stigma` | timestamp 754 s |
| [`07_Burial_-_Archangel.mp3`](/trackparse/playground/#track=07_Burial_-_Archangel.mp3) | Burial | `Archangel` | filename mode, position 7 |
| [`"Numb" by Linkin Park`](/trackparse/playground/#track=%22Numb%22%20by%20Linkin%20Park&mode=youtube) *(youtube)* | Linkin Park | `Numb` | warning [`bySplit`](/trackparse/reference/warnings/#track-warnings) |
| [`Blink-182-All The Small Things`](/trackparse/playground/#track=Blink-182-All%20The%20Small%20Things&mode=youtube) *(youtube)* | Blink-182 | `All The Small Things` | warning [`unspacedDashSplit`](/trackparse/reference/warnings/#track-warnings) |
| [`Кино - Группа крови (Remastered 2019)`](/trackparse/playground/#track=%D0%9A%D0%B8%D0%BD%D0%BE%20-%20%D0%93%D1%80%D1%83%D0%BF%D0%BF%D0%B0%20%D0%BA%D1%80%D0%BE%D0%B2%D0%B8%20%28Remastered%202019%29) | Кино | `Группа крови` | remaster, 2019 |
| [`ID - ID (ID Remix)`](/trackparse/playground/#track=ID%20-%20ID%20%28ID%20Remix%29) |  | `ID` | unknown artist, title and remixer |
| [`AC/DC - Back In Black`](/trackparse/playground/#track=AC%2FDC%20-%20Back%20In%20Black) | AC/DC | `Back In Black` |  |
| [`2 Unlimited - No Limit`](/trackparse/playground/#track=2%20Unlimited%20-%20No%20Limit) | 2 Unlimited | `No Limit` | not a track number |
| [`Mumford & Sons - Little Lion Man`](/trackparse/playground/#track=Mumford%20%26%20Sons%20-%20Little%20Lion%20Man) | Mumford & Sons | `Little Lion Man` | not split |
| [`The Rolling Stones - (I Can't Get No) Satisfaction`](/trackparse/playground/#track=The%20Rolling%20Stones%20-%20%28I%20Can%27t%20Get%20No%29%20Satisfaction) | The Rolling Stones | `(I Can't Get No) Satisfaction` | kept as is |

A few of these deserve a comment.

**`2 Unlimited` is not track 2.** A leading number only counts as a track number when it's written
like one: `01.` or `1)`, a zero-padded `01 `, `3 - Artist - Title`, or `07_` in a file name. A plain
`2 ` in front of a word is part of the name, which is what you want for `2 Unlimited`, `50 Cent` and
`808 State`. The rule is [R4.2](/trackparse/reference/spec/#r4-prefixes-timestamp-and-position:~:text=R4.2%20Position), and
[Track numbers](/trackparse/guides/junk-and-flags/#track-numbers) has more examples.

**`Mumford & Sons` stays whole.** `&` is a joiner, but `& Sons`, `& Co.` or `& His Orchestra`
never start a second artist. `& The` does split, on purpose: `Noisia & The Upbeats` is two
acts ([R7.3](/trackparse/reference/spec/#r7-artist-side:~:text=R7.3%20Credit%2Dlist%20splitting), with the list in
[`no-split-before.json`](https://github.com/4matic/trackparse/blob/main/spec/data/no-split-before.json)).
`Chase & Status` splits too, because nothing in the string tells it apart from two artists.
Names like that go in [`knownArtists`](/trackparse/guides/artists/#knownartists) (`known_artists` in
Python and Rust, `KnownArtists` in Go, `trackparse_parse_options_add_known_artist` in C).

**The Rolling Stones keep their brackets.** `(I Can't Get No)` is not a version, a feat or junk, so
it stays in the title. That's the rule: anything unrecognized is left alone
([R5.8](/trackparse/reference/spec/#r5-group-classification:~:text=R5.8%20Unknown)).

**Two rows needed `mode: "youtube"`.** `"Numb" by Linkin Park` and `Blink-182-All The Small Things`
are fine guesses on YouTube and bad ones in a music library, so the default `clean` mode won't make
them. In `youtube` mode it does, and it tells you through `warnings`
([`bySplit`](/trackparse/reference/warnings/#track-warnings:~:text=bySplit) and
[`unspacedDashSplit`](/trackparse/reference/warnings/#track-warnings:~:text=unspacedDashSplit)). In
Python the option is `mode="youtube"`, in Rust `ParseOptions::new().mode(ModeOption::Youtube)`, in
Go `trackparse.ParseOptions{Mode: trackparse.ModeYouTube}`, in C
`trackparse_parse_options_set_mode(options, TRACKPARSE_MODE_YOUTUBE)`. See
[Modes](/trackparse/guides/modes/#youtube-mode) and the [`mode` option](/trackparse/reference/options/#parse-options).

## Labels after a bullet

Radio show tracklists often write the record label after the title, behind a bullet, and sometimes
a note after a second one:

| Input | Artists | Title | Other fields |
|---|---|---|---|
| [`Tom Finster - Age Of Spin • WEAREHUMANS`](/trackparse/playground/#track=Tom%20Finster%20-%20Age%20Of%20Spin%20%E2%80%A2%20WEAREHUMANS) | Tom Finster | `Age Of Spin` | label `WEAREHUMANS` |
| [`Scrufizzer & Machinedrum - WHO (K-LONE VIP) • IAMSIAM`](/trackparse/playground/#track=Scrufizzer%20%26%20Machinedrum%20-%20WHO%20%28K-LONE%20VIP%29%20%E2%80%A2%20IAMSIAM) | Scrufizzer, Machinedrum | `WHO` | vip by K-LONE, label `IAMSIAM` |
| [`Noisia - concussion • VISION • VISION RADIO RELOAD`](/trackparse/playground/#track=Noisia%20-%20concussion%20%E2%80%A2%20VISION%20%E2%80%A2%20VISION%20RADIO%20RELOAD) | Noisia | `concussion` | label `VISION`, junk: note `VISION RADIO RELOAD` |
| [`Noisia • Phace - Stigma • VISION`](/trackparse/playground/#track=Noisia%20%E2%80%A2%20Phace%20-%20Stigma%20%E2%80%A2%20VISION) | `Noisia • Phace` | `Stigma` | label `VISION` |
| [`Noisia - Stigma • HOSPITAL (2024)`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%E2%80%A2%20HOSPITAL%20%282024%29) | Noisia | `Stigma` | label `HOSPITAL (2024)`, no year |
| [`Noisia - Stigma • HOSPITAL [Official Video]`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%E2%80%A2%20HOSPITAL%20%5BOfficial%20Video%5D) | Noisia | `Stigma` | label `HOSPITAL`, junk: video, mode youtube |
| [`Noisia - Stigma • FREE DOWNLOAD`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%E2%80%A2%20FREE%20DOWNLOAD) | Noisia | `Stigma` | no label, junk: promo, mode youtube |
| [`Noisia - Stigma • Part 2`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%E2%80%A2%20Part%202) | Noisia | `Stigma` | label `Part 2` (see below) |

The rule is [R8.7](/trackparse/reference/spec/#r8-title-side:~:text=R8.7%20Bullet%20suffix). It fires
on a *spaced bullet*: `•` (U+2022) with a space on each side, outside every bracket, somewhere after
the first spaced dash. Everything after the first such bullet is the tail, cut at each spaced
bullet:

- The first tail segment is [`label`](/trackparse/reference/parse/#parsedtrack), as written (case
  kept, no lookup). Each later segment goes to `junk` with the kind `note`.
- A bracket of junk in the tail goes to `junk` as usual, and a segment made only of junk phrases
  (`FREE DOWNLOAD`, `OUT NOW`) becomes junk instead of a label. Other brackets stay in the label
  word for word, so `HOSPITAL (2024)` sets no `year`.
- The track itself is the part before the first bullet. Versions, features, junk and the year come
  from there only, which is why `WHO (K-LONE VIP) • IAMSIAM` keeps its VIP as a version.
- Auto mode still sees the tail: `• FREE DOWNLOAD` and `• HOSPITAL [Official Video]` pick
  [youtube mode](/trackparse/guides/modes/#how-auto-decides), as they would without the bullet.
  The mode then decides how the head is read: `Noisia - Stigma HD • VISION HD` ends with the
  phrase `HD`, so it is a YouTube title, and youtube mode takes the `HD` off the head too (title
  `Stigma`, junk quality `HD`, label `VISION HD`).

It does not fire, and the bullet stays where it is, when:

- the bullet is not spaced ([`Noisia - Stigma•VISION`](/trackparse/playground/#track=Noisia%20-%20Stigma%E2%80%A2VISION)) or is
  inside brackets ([`Noisia - Stigma (A • B Remix)`](/trackparse/playground/#track=Noisia%20-%20Stigma%20%28A%20%E2%80%A2%20B%20Remix%29):
  a remix by `A • B`);
- it comes before the dash ([`Noisia • Stigma - VISION`](/trackparse/playground/#track=Noisia%20%E2%80%A2%20Stigma%20-%20VISION): artist
  `Noisia • Stigma`), or there is no spaced dash at all (`Intro • VISION`, `Noisia | Stigma • VISION`);
- the dot is a different character: `·` (U+00B7), `∙` (U+2219) and `●` (U+25CF) are not bullets;
- a segment is empty (`Noisia - Stigma •  • VISION`).

One accepted false positive: a bullet that is part of the title reads as a label too.
`Noisia - Stigma • Part 2` gives the title `Stigma` and the label `Part 2`. Quotes don't protect
it either: `Noisia - "Stigma • VISION"` gives the title `"Stigma` and the label `VISION"`. The junk kind `label`
is something else: a label name in brackets such as `[NCS Release]`, which leaves the `label` field
`null`. [`format`](/trackparse/guides/formatting/#labels) writes the label back after the title.

## In a tracklist

Lines of a pasted tracklist go through
[`parseTracklist`](/trackparse/guides/tracklists/) (`parse_tracklist` in Python and Rust,
`ParseTracklist` in Go, `trackparse_parse_tracklist` in C; the [Tracklist API](/trackparse/reference/tracklist/) has every field), which
also reads list bullets, Markdown emphasis and `w/` lines. Each input below is one line; click it to open it in the playground's
tracklist tab.

| Line | Artists | Title | Other fields |
|---|---|---|---|
| [`* 00:00 Noisia - Stigma`](/trackparse/playground/#list=*%2000%3A00%20Noisia%20-%20Stigma) | Noisia | `Stigma` | time 0 s, bullet in `lead` |
| [`**[**00:00**] 10. Nitepunk - ID**`](/trackparse/playground/#list=**%5B**00%3A00**%5D%2010.%20Nitepunk%20-%20ID**) | Nitepunk | `ID` | time 0 s, position 10, `**` kept in [`parts`](/trackparse/guides/tracklists/#nothing-is-lost) |
| [`00:00 **Noisia** - _Stigma_`](/trackparse/playground/#list=00%3A00%20**Noisia**%20-%20_Stigma_) | Noisia | `Stigma` | time 0 s |
| [`w/ Nitepunk & Habstrakt - Point`](/trackparse/playground/#list=w%2F%20Nitepunk%20%26%20Habstrakt%20-%20Point) | Nitepunk, Habstrakt | `Point` | layered |
| [`00:00 P*nk - Raise Your Glass`](/trackparse/playground/#list=00%3A00%20P*nk%20-%20Raise%20Your%20Glass) | P\*nk | `Raise Your Glass` | the star stays |

## By topic

- [Artists and credits](/trackparse/guides/artists/): joiners, features, producers, `knownArtists`.
- [Versions and remixes](/trackparse/guides/versions/): the 27 types and how they're read.
- [Junk, flags and prefixes](/trackparse/guides/junk-and-flags/): noise, labels after a bullet, years, track numbers, cue times.
- [Modes](/trackparse/guides/modes/): what changes for YouTube titles, file names and tracklist lines.
- [Formatting](/trackparse/guides/formatting/): writing a parsed track back as a string.
- [Tracklists](/trackparse/guides/tracklists/) and
  [Editing tracklists](/trackparse/guides/editing-tracklists/): whole lists with times.

For the exact shape of the result, see [`parse` and `ParsedTrack`](/trackparse/reference/parse/).
