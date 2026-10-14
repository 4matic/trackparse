// Rendering helpers shared by the landing-page demo and the playground.
// trackparse returns no offsets (by design, see SPEC R0.5), so the highlight finds each extracted
// value in the input again. It is a display aid only; the parsed object is the real answer.

import type { Artist, ParsedTrack, Version } from "trackparse";
import { icon } from "./icons";

export type Kind =
  | "position"
  | "primary"
  | "featured"
  | "producer"
  | "title"
  | "version"
  | "junk"
  | "year";

export interface Segment {
  text: string;
  kind: Kind | null;
}

export const KIND_LABEL: Record<Kind, string> = {
  position: "position / time",
  primary: "artist",
  featured: "featured",
  producer: "producer",
  title: "title",
  version: "version",
  junk: "junk",
  year: "year",
};

export function escapeHtml(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** Cut `input` into segments coloured by the field each piece of text ended up in. */
export function highlight(input: string, track: ParsedTrack): Segment[] {
  const owner: (Kind | null)[] = new Array(input.length).fill(null);
  const lower = input.toLowerCase();

  const mark = (needle: string | null | undefined, kind: Kind) => {
    if (!needle) return;
    const hay = [input, lower];
    const keys = [needle, needle.toLowerCase()];
    for (let pass = 0; pass < 2; pass++) {
      let from = 0;
      for (;;) {
        const at = hay[pass].indexOf(keys[pass], from);
        if (at < 0) break;
        const end = at + needle.length;
        let free = true;
        for (let i = at; i < end; i++) {
          if (owner[i] !== null) {
            free = false;
            break;
          }
        }
        if (free) {
          for (let i = at; i < end; i++) owner[i] = kind;
          return;
        }
        from = at + 1;
      }
    }
  };

  for (const v of track.versions) mark(v.raw, "version");
  for (const j of track.junk) mark(j.raw, "junk");
  mark(track.position?.raw, "position");
  mark(track.timestamp?.raw, "position");
  mark(track.title, "title");
  for (const a of track.artists) mark(a.name, roleKind(a));
  if (track.year !== null) mark(String(track.year), "year");

  const out: Segment[] = [];
  for (let i = 0; i < input.length; i++) {
    const k = owner[i];
    const last = out[out.length - 1];
    if (last && last.kind === k) last.text += input[i];
    else out.push({ text: input[i], kind: k });
  }
  return out;
}

export function roleKind(a: Artist): Kind {
  if (a.role === "featured") return "featured";
  if (a.role === "producer") return "producer";
  if (a.role === "remixer") return "version";
  return "primary";
}

export function segmentsHtml(segs: Segment[]): string {
  return segs
    .map((s) =>
      s.kind
        ? `<mark class="tp-seg tp-${s.kind}" title="${KIND_LABEL[s.kind]}">${escapeHtml(s.text)}</mark>`
        : `<span class="tp-gap">${escapeHtml(s.text)}</span>`,
    )
    .join("");
}

export function chip(text: string, kind: string, title?: string): string {
  const t = title ? ` title="${escapeHtml(title)}"` : "";
  return `<span class="tp-chip tp-${kind}"${t}>${escapeHtml(text)}</span>`;
}

function versionLine(v: Version): string {
  const bits: string[] = [chip(v.type, "version")];
  for (const a of v.artists) bits.push(`by <strong>${escapeHtml(a.name)}</strong>`);
  for (const m of v.modifiers) bits.push(chip(`+${m}`, "version"));
  if (v.descriptor) bits.push(`<em>${escapeHtml(v.descriptor)}</em>`);
  if (v.year !== null) bits.push(chip(String(v.year), "year"));
  if (v.unknownArtist) bits.push(chip("unknown remixer", "junk"));
  return bits.join(" ");
}

/** The field cards shown under the highlighted string. */
export function fieldsHtml(t: ParsedTrack): string {
  const rows: [string, string][] = [];
  rows.push([
    "Artists",
    t.artists.length
      ? t.artists
          .map(
            (a) =>
              `<span class="tp-artist">${a.joiner ? `<span class="tp-joiner">${escapeHtml(a.joiner)}</span>` : ""}<strong>${escapeHtml(a.name)}</strong> ${chip(a.role, roleKind(a))}</span>`,
          )
          .join("")
      : `<span class="tp-none">${t.flags.unknownArtist ? "unknown (ID)" : "none"}</span>`,
  ]);
  rows.push([
    "Title",
    t.title
      ? `<strong class="tp-title-text">${escapeHtml(t.title)}</strong>${t.flags.unknownTitle ? ` ${chip("unknown (ID)", "junk")}` : ""}`
      : `<span class="tp-none">empty</span>`,
  ]);
  if (t.versions.length)
    rows.push(["Versions", t.versions.map((v) => `<div>${versionLine(v)}</div>`).join("")]);
  if (t.junk.length)
    rows.push([
      "Junk",
      t.junk.map((j) => `${chip(j.kind, "junk")} <s>${escapeHtml(j.raw)}</s>`).join("<br>"),
    ]);
  if (t.position) rows.push(["Position", chip(`#${t.position.number}`, "position")]);
  if (t.timestamp)
    rows.push(["Timestamp", chip(`${t.timestamp.raw} = ${t.timestamp.seconds} s`, "position")]);
  if (t.year !== null) rows.push(["Year", chip(String(t.year), "year")]);
  const flags = (["explicit", "clean"] as const).filter((f) => t.flags[f]);
  if (flags.length) rows.push(["Flags", flags.map((f) => chip(f, "year")).join(" ")]);
  rows.push(["Mode", `<code>${t.mode}</code>`]);
  if (t.warnings.length)
    rows.push([
      "Warnings",
      t.warnings.map((w) => `<span class="tp-warn">${icon.warning}${escapeHtml(w)}</span>`).join(" "),
    ]);
  return `<dl class="tp-fields">${rows
    .map(([k, v]) => `<div class="tp-field"><dt>${k}</dt><dd>${v}</dd></div>`)
    .join("")}</dl>`;
}

/** Minimal JSON syntax colouring for the output panel. */
export function jsonHtml(value: unknown): string {
  const json = JSON.stringify(value, null, 2);
  return escapeHtml(json).replace(
    /(&quot;(?:[^&]|&(?!quot;))*?&quot;)(\s*:)?|\b(true|false|null)\b|(-?\d+(?:\.\d+)?)/g,
    (m, str, colon, lit, num) => {
      if (str) return colon ? `<span class="j-key">${str}</span>${colon}` : `<span class="j-str">${str}</span>`;
      if (lit) return `<span class="j-lit">${lit}</span>`;
      if (num) return `<span class="j-num">${num}</span>`;
      return m;
    },
  );
}
