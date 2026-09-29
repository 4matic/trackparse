import { describe, expect, it } from "vitest";
import {
  artistById as A,
  extraById,
  featMarkerById,
  joinerById as J,
  junkById,
  modeById,
  prefixById,
  producerMarkerById,
  sepById,
  titleById,
  versionById,
} from "../src/atoms.js";
import { type Composition, type CreditList, dedup, problem, render, single, splitCredit } from "../src/model.js";
import { base } from "../src/scenario.js";

const list = (names: string[], joiners: string[]): CreditList => ({ names: names.map(A), joiners: joiners.map(J) });
const main = { mainArtistList: true, splitAnd: "auto" as const };
const other = { mainArtistList: false, splitAnd: "auto" as const };
const names = (xs: { name: string }[]) => xs.map((x) => x.name);

describe("splitCredit (R7.3 on atoms)", () => {
  it("splits spaced and tight joiners, keeping the joiner as written", () => {
    const r = splitCredit(list(["noisia", "mefjus", "phace"], ["vs-dot-cap", "semicolon"]), main, null, "primary", "artist");
    expect(r).toEqual([
      { name: "Noisia", role: "primary", joiner: null, source: "artist" },
      { name: "Mefjus", role: "primary", joiner: "Vs.", source: "artist" },
      { name: "Phace", role: "primary", joiner: ";", source: "artist" },
    ]);
  });

  it("`and` splits only after a comma in auto mode", () => {
    expect(names(splitCredit(list(["andy-c", "noisia"], ["and"]), main, null, "primary", "artist"))).toEqual([
      "Andy C and Noisia",
    ]);
    expect(names(splitCredit(list(["camo", "krooked", "noisia"], ["comma", "and"]), main, null, "primary", "artist"))).toEqual(
      ["Camo", "Krooked", "Noisia"],
    );
    expect(
      names(splitCredit(list(["camo", "krooked", "noisia"], ["comma", "and"]), { ...main, splitAnd: "never" }, null, "primary", "artist")),
    ).toEqual(["Camo", "Krooked and Noisia"]);
    expect(
      names(splitCredit(list(["andy-c", "noisia"], ["and-upper"]), { ...main, splitAnd: "always" }, null, "primary", "artist")),
    ).toEqual(["Andy C", "Noisia"]);
  });

  it("`with` splits only in the main artist list; uppercase X never splits", () => {
    expect(names(splitCredit(list(["camo", "krooked"], ["with"]), main, null, "primary", "artist"))).toEqual(["Camo", "Krooked"]);
    expect(names(splitCredit(list(["camo", "krooked"], ["with"]), other, "ft.", "featured", "title"))).toEqual(["Camo with Krooked"]);
    expect(names(splitCredit(list(["camo", "krooked"], ["x-upper"]), main, null, "primary", "artist"))).toEqual(["Camo X Krooked"]);
  });

  it("first featured name carries the marker", () => {
    const r = splitCredit(list(["camo", "krooked"], ["amp"]), other, "feat.", "featured", "title");
    expect(r.map((a) => a.joiner)).toEqual(["feat.", "&"]);
  });
});

describe("dedup (R9.2)", () => {
  it("keeps the first occurrence, comparing R0.4 keys", () => {
    const r = dedup([
      { name: "Tiësto", role: "primary", joiner: null, source: "artist" },
      { name: "TIESTO", role: "featured", joiner: "ft.", source: "title" },
      { name: "Phace", role: "featured", joiner: "&", source: "title" },
    ]);
    expect(names(r)).toEqual(["Tiësto", "Phace"]);
  });

  it("producers are a separate class, deduped only among producers", () => {
    const r = dedup([
      { name: "Noisia", role: "primary", joiner: null, source: "artist", list: 0 },
      { name: "NOISIA", role: "producer", joiner: "prod. by", source: "title", list: 2 },
      { name: "noisia", role: "producer", joiner: "&", source: "title", list: 2 },
    ]);
    expect(r.map((a) => [a.name, a.role, a.joiner])).toEqual([
      ["Noisia", "primary", null],
      ["NOISIA", "producer", "prod. by"],
    ]);
  });
});

function comp(parts: Partial<Composition>): Composition {
  return base({ title: titleById("diplodocus"), mode: modeById("clean"), main: single(A("noisia")), ...parts });
}

describe("problem() rejects ambiguous compositions", () => {
  it("311 followed by a dash suffix (R4.2c would take it as a position)", () => {
    const c = comp({ main: single(A("311")), versions: [{ atom: versionById("vip-bare"), delim: "-", credits: [] }] });
    expect(problem(c)).toMatch(/digits-only/);
    expect(problem({ ...c, prefix: prefixById("pos-01-dot") })).toBeNull();
  });

  it("with + stopword (`(with The xx)` stays Unknown)", () => {
    const c = comp({ feat: { placement: "title-paren", marker: featMarkerById("with"), credit: single(A("the-xx")) } });
    expect(problem(c)).toBe("with + stopword");
  });

  it("inline title feat followed by unbracketed junk", () => {
    const c = comp({
      mode: modeById("youtube"),
      feat: { placement: "title-inline", marker: featMarkerById("ft-dot"), credit: single(A("camo")) },
      junk: [{ atom: junkById("hd"), form: "trailing" }],
    });
    expect(problem(c)).toMatch(/trailing junk/);
  });

  it("youtube-only forms outside youtube, unspaced dash in clean", () => {
    expect(problem(comp({ junk: [{ atom: junkById("hd"), form: "pipe" }] }))).toMatch(/youtube-only/);
    expect(problem(comp({ sep: sepById("unspaced") }))).toMatch(/clean/);
  });

  it("Malcolm X next to an x joiner", () => {
    expect(problem(comp({ main: list(["malcolm-x", "noisia"], ["x"]) }))).toMatch(/X-name/);
  });
});

describe("render() spot checks (hand-computed expectations)", () => {
  it("youtube: position, feat on artist side, remix, junk, platform", () => {
    const r = render(
      comp({
        mode: modeById("youtube-yt"),
        prefix: prefixById("pos-01-dot"),
        main: single(A("fox-stevenson")),
        feat: { placement: "artist-inline", marker: featMarkerById("ft-dot"), credit: list(["camo", "krooked"], ["amp"]) },
        title: titleById("bruises"),
        versions: [{ atom: versionById("remix"), delim: "(", credits: [single(A("magnetude"))] }],
        junk: [{ atom: junkById("official-video"), form: "square" }],
      }),
      "t",
    );
    expect(r.input).toBe("01. Fox Stevenson ft. Camo & Krooked - Bruises (Magnetude Remix) [Official Video] - YouTube");
    expect(r.options).toEqual({ mode: "youtube" });
    expect(r.expected).toEqual({
      position: { raw: "01", number: 1 },
      timestamp: null,
      artists: [
        "Fox Stevenson",
        { name: "Camo", role: "featured", joiner: "ft.", source: "artist" },
        { name: "Krooked", role: "featured", joiner: "&", source: "artist" },
      ],
      title: "Bruises",
      fullTitle: "Bruises (Magnetude Remix)",
      versions: [{ type: "remix", raw: "Magnetude Remix", artists: ["Magnetude"], delimiter: "(" }],
      junk: [
        { raw: "Official Video", kind: "video" },
        { raw: "YouTube", kind: "platform" },
      ],
    });
  });

  it("dash suffixes, extras and producer keep text order", () => {
    const r = render(
      comp({
        title: titleById("song-part-2"),
        feat: { placement: "title-square", marker: featMarkerById("featuring"), credit: single(A("mo")) },
        producer: { marker: producerMarkerById("prod-by"), credit: single(A("enei")), form: "paren" },
        versions: [
          { atom: versionById("extended-vip-credit"), delim: "{", credits: [single(A("netsky"))] },
          { atom: versionById("remastered-2015"), delim: "-", credits: [] },
        ],
        extras: [extraById("explicit")],
      }),
      "t",
      { fullVersions: true },
    );
    expect(r.input).toBe(
      "Noisia - Song (Part 2) [featuring Mø] (prod. by Enei) {Netsky Extended VIP Mix} (Explicit) - Remastered 2015",
    );
    expect(r.expected.artists).toEqual([
      "Noisia",
      { name: "Mø", role: "featured", joiner: "featuring", source: "title" },
      { name: "Enei", role: "producer", joiner: "prod. by", source: "title" },
    ]);
    expect(r.expected.title).toBe("Song (Part 2)");
    expect(r.expected.fullTitle).toBe("Song (Part 2) {Netsky Extended VIP Mix} - Remastered 2015");
    expect(r.expected.versions).toEqual([
      {
        type: "vip", raw: "Netsky Extended VIP Mix", artists: ["Netsky"], modifiers: ["extended"], descriptor: null,
        year: null, unknownArtist: false, delimiter: "{",
      },
      {
        type: "remaster", raw: "Remastered 2015", artists: [], modifiers: [], descriptor: null, year: 2015,
        unknownArtist: false, delimiter: "-",
      },
    ]);
    expect(r.expected.flags).toEqual({ explicit: true, clean: false });
    expect(r.expected.year).toBeNull();
  });

  it("filename with underscores and a numeric artist", () => {
    const r = render(
      comp({ mode: modeById("filename-mp3-us"), main: single(A("2-unlimited")), title: titleById("one-more-time") }),
      "t",
    );
    expect(r.input).toBe("2_Unlimited_-_One_More_Time.mp3");
    expect(r.expected).toMatchObject({ position: null, artists: ["2 Unlimited"], title: "One More Time" });
    expect(r.expected.fullTitle).toBeUndefined();
  });

  it("R7.6: a removed first featured name passes the marker on (dedup)", () => {
    const r = render(
      comp({
        allowDup: true,
        feat: { placement: "title-paren", marker: featMarkerById("feat-dot"), credit: list(["noisia", "phace"], ["amp"]) },
      }),
      "t",
    );
    expect(r.expected.artists).toEqual(["Noisia", { name: "Phace", role: "featured", joiner: "feat.", source: "title" }]);
  });

  it("R5.7.2: version/cover credit spans become descriptors", () => {
    const r = render(
      comp({ versions: [{ atom: versionById("cover"), delim: "(", credits: [list(["camo", "krooked"], ["amp"])] }] }),
      "t",
      { fullVersions: true },
    );
    expect(r.expected.versions).toEqual([
      {
        type: "cover", raw: "Camo & Krooked Cover", artists: [], modifiers: [], descriptor: "Camo & Krooked",
        year: null, unknownArtist: false, delimiter: "(",
      },
    ]);
  });

  it("R8.3: undotted markers are text on the title side", () => {
    const c = comp({ feat: { placement: "title-inline", marker: featMarkerById("ft"), credit: single(A("camo")) } });
    expect(problem(c)).toMatch(/text on the title side/);
  });

  it("auto mode resolution", () => {
    const genreOnly = comp({ mode: modeById("auto"), junk: [{ atom: junkById("dubstep"), form: "paren" }] });
    expect(render(genreOnly, "t").expected.mode).toBe("clean");
    const video = comp({ mode: modeById("auto"), junk: [{ atom: junkById("official-video"), form: "paren" }] });
    expect(render(video, "t").expected.mode).toBe("youtube");
    expect(render(comp({ mode: modeById("auto-mp3") }), "t").expected.mode).toBe("filename");
  });
});
