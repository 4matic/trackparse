---
title: What it understands
description: Real inputs and what trackparse returns for them, from feats and remixes to file names and YouTube junk.
---

Every row below is a real result of `parse(input)` with default options, except the two marked
*youtube*. Click an input to open it in the [playground](/trackparse/playground/) and see the full
output.

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
| [`Porter Robinson - Shelter - YouTube`](/trackparse/playground/#track=Porter%20Robinson%20-%20Shelter%20-%20YouTube) | Porter Robinson | `Shelter` | junk: platform |
| [`01. Aphex Twin - Xtal`](/trackparse/playground/#track=01.%20Aphex%20Twin%20-%20Xtal) | Aphex Twin | `Xtal` | position 1 |
| [`[12:34] Noisia - Stigma`](/trackparse/playground/#track=%5B12%3A34%5D%20Noisia%20-%20Stigma) | Noisia | `Stigma` | timestamp 754 s |
| [`07_Burial_-_Archangel.mp3`](/trackparse/playground/#track=07_Burial_-_Archangel.mp3) | Burial | `Archangel` | filename mode, position 7 |
| [`"Numb" by Linkin Park`](/trackparse/playground/#track=%22Numb%22%20by%20Linkin%20Park) *(youtube)* | Linkin Park | `Numb` | ⚠ bySplit |
| [`Blink-182-All The Small Things`](/trackparse/playground/#track=Blink-182-All%20The%20Small%20Things) *(youtube)* | Blink-182 | `All The Small Things` | ⚠ unspacedDashSplit |
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
`808 State`.

**`Mumford & Sons` stays whole.** `&` is a joiner, but `& Sons`, `& Co.` or `& His Orchestra`
never start a second artist. `& The` does split, on purpose: `Noisia & The Upbeats` really is two
acts. `Chase & Status` splits too, because nothing in the string tells it apart from two artists.
Names like that go in `knownArtists`.

**The Rolling Stones keep their brackets.** `(I Can't Get No)` is not a version, a feat or junk, so
it stays in the title. That's the rule: anything unrecognised is left alone.

**Two rows needed `mode: "youtube"`.** `"Numb" by Linkin Park` and `Blink-182-All The Small Things`
are fine guesses on YouTube and bad ones in a music library, so the default `clean` mode won't make
them. In `youtube` mode it does, and it tells you through `warnings`. See [Modes](/trackparse/guides/modes/).

## By topic

- [Artists and credits](/trackparse/guides/artists/): joiners, features, producers, `knownArtists`.
- [Versions and remixes](/trackparse/guides/versions/): the 27 types and how they're read.
- [Junk, flags and prefixes](/trackparse/guides/junk-and-flags/): noise, years, track numbers, cue times.
