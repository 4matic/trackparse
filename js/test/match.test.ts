import { describe, expect, it } from "vitest";
import { match, matchExact, matchPartial } from "./match.js";

describe("matchPartial", () => {
  it("ignores keys not in expected", () => {
    expect(matchPartial({ a: 1 }, { a: 1, b: 2 })).toBeNull();
  });
  it("requires expected keys to exist", () => {
    expect(matchPartial({ a: 1, c: 3 }, { a: 1 })).toMatch(/\.c: missing/);
  });
  it("explicit null must be null", () => {
    expect(matchPartial({ a: null }, { a: null })).toBeNull();
    expect(matchPartial({ a: null }, { a: 0 })).not.toBeNull();
    expect(matchPartial({ a: null }, {})).not.toBeNull();
  });
  it("arrays need the same length and order", () => {
    expect(matchPartial([1, 2], [1, 2])).toBeNull();
    expect(matchPartial([1, 2], [2, 1])).not.toBeNull();
    expect(matchPartial([1], [1, 2])).not.toBeNull();
    expect(matchPartial([], [])).toBeNull();
  });
  it("scalars use strict equality, no case folding", () => {
    expect(matchPartial("a", "A")).not.toBeNull();
    expect(matchPartial(1, "1")).not.toBeNull();
    expect(matchPartial(false, false)).toBeNull();
  });
  it("supports the artist shorthand", () => {
    const actual = {
      artists: [
        { name: "Noisia", role: "primary" },
        { name: "Foreign Beggars", role: "featured" },
      ],
    };
    expect(
      matchPartial({ artists: ["Noisia", { name: "Foreign Beggars", role: "featured" }] }, actual),
    ).toBeNull();
    expect(matchPartial({ artists: ["Noisia"] }, actual)).not.toBeNull();
    expect(matchPartial({ artists: ["Noisia", "Foreign"] }, actual)).not.toBeNull();
    expect(
      matchPartial({ artists: ["Noisia", { name: "Foreign Beggars", role: "primary" }] }, actual),
    ).not.toBeNull();
  });
  it("applies the shorthand inside versions and to parseArtists results", () => {
    expect(
      matchPartial(
        { versions: [{ type: "remix", artists: ["X"] }] },
        {
          versions: [{ type: "remix", raw: "X Remix", artists: [{ name: "X", role: "remixer" }] }],
        },
      ),
    ).toBeNull();
    expect(matchPartial(["A", "B"], [{ name: "A" }, { name: "B" }])).toBeNull();
  });
  it("string expectation against a string array element is plain equality", () => {
    expect(matchPartial(["extended"], ["extended"])).toBeNull();
    expect(matchPartial(["extended"], ["vip"])).not.toBeNull();
  });
});

describe("matchExact", () => {
  it("needs complete equality", () => {
    expect(matchExact({ a: 1 }, { a: 1 })).toBeNull();
    expect(matchExact({ a: 1 }, { a: 1, b: 2 })).not.toBeNull();
    expect(matchExact([{ a: [1] }], [{ a: [1] }])).toBeNull();
  });
  it("does not allow the artist shorthand", () => {
    expect(matchExact(["A"], [{ name: "A" }])).not.toBeNull();
  });
  it("dispatches by mode", () => {
    expect(match({ a: 1 }, { a: 1, b: 1 }, "partial")).toBeNull();
    expect(match({ a: 1 }, { a: 1, b: 1 }, "exact")).not.toBeNull();
  });
});
