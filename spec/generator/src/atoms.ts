/**
 * Ground-truth atoms. Each atom carries its rendered text AND its expected
 * contribution to the parse result. Expected output of a composed case is computed
 * only from these (see model.ts) \u2014 never by running a parser.
 *
 * Atom ids are lowercase ASCII (`[a-z0-9-]`), unique within their pool; case ids are
 * built by joining them with `.`.
 */

export interface Atom {
  id: string;
  tags?: string[];
}

// ---------------------------------------------------------------- artists

export type ArtistCat = "plain" | "cyrillic" | "diacritics" | "cjk" | "symbols" | "trap" | "numeric";

export interface ArtistAtom extends Atom {
  text: string;
  /** Expected credits contributed by this atom on its own (always exactly one name). */
  expect: { name: string }[];
  cat: ArtistCat;
  /** Contains a joiner guarded by data/no-split-before.json (`Mumford & Sons`). */
  guarded?: boolean;
}

function artist(id: string, text: string, cat: ArtistCat, extra: Partial<ArtistAtom> = {}): ArtistAtom {
  return { id, text, expect: [{ name: text }], cat, tags: [cat], ...extra };
}

export const ARTISTS: ArtistAtom[] = [
  // plain
  artist("noisia", "Noisia", "plain"),
  artist("mefjus", "Mefjus", "plain"),
  artist("phace", "Phace", "plain"),
  artist("culture-shock", "Culture Shock", "plain"),
  artist("fox-stevenson", "Fox Stevenson", "plain"),
  artist("gydra", "Gydra", "plain"),
  artist("teddy-killerz", "Teddy Killerz", "plain"),
  artist("camo", "Camo", "plain"),
  artist("krooked", "Krooked", "plain"),
  artist("foreign-beggars", "Foreign Beggars", "plain"),
  artist("magnetude", "Magnetude", "plain"),
  artist("sub-focus", "Sub Focus", "plain"),
  artist("netsky", "Netsky", "plain"),
  artist("hybrid-minds", "Hybrid Minds", "plain"),
  artist("black-sun-empire", "Black Sun Empire", "plain"),
  artist("aphex-twin", "Aphex Twin", "plain"),
  artist("skrillex", "Skrillex", "plain"),
  artist("the-upbeats", "The Upbeats", "plain"),
  artist("current-value", "Current Value", "plain"),
  artist("enei", "Enei", "plain"),
  artist("kanye-west", "Kanye West", "plain"),
  artist("daft-punk", "Daft Punk", "plain"),
  artist("massive-attack", "Massive Attack", "plain"),
  artist("billie-eilish", "Billie Eilish", "plain"),
  // cyrillic
  artist("zveri", "Звери", "cyrillic"),
  artist("kino", "Кино", "cyrillic"),
  artist("lyapis", "Ляпис Трубецкой", "cyrillic"),
  artist("katya-chekhova", "Катя Чехова", "cyrillic"),
  artist("mumiy-troll", "Мумий Тролль", "cyrillic"),
  artist("splin", "Сплин", "cyrillic"),
  // diacritics
  artist("sigur-ros", "Sigur Rós", "diacritics"),
  artist("beyonce", "Beyoncé", "diacritics"),
  artist("mo", "Mø", "diacritics"),
  artist("royksopp", "Røyksopp", "diacritics"),
  artist("tiesto", "Tiësto", "diacritics"),
  artist("motley-crue", "Mötley Crüe", "diacritics"),
  artist("bjork", "Björk", "diacritics"),
  artist("celine-dion", "Céline Dion", "diacritics"),
  // CJK / Hangul
  artist("utada", "宇多田ヒカル", "cjk"),
  artist("bts", "방탄소년단", "cjk"),
  artist("sakamoto", "坂本龍一", "cjk"),
  artist("jay-chou", "周杰伦", "cjk"),
  // symbols
  artist("asap-rocky", "A$AP Rocky", "symbols"),
  artist("pink", "P!nk", "symbols"),
  artist("will-i-am", "will.i.am", "symbols"),
  artist("deadmau5", "deadmau5", "symbols"),
  artist("mr-oizo", "Mr. Oizo", "symbols"),
  artist("ant-shift", "Ant+Shift", "symbols"),
  artist("ac-dc", "AC/DC", "symbols"),
  artist("kesha", "Ke$ha", "symbols"),
  artist("johny-jpr", "Johny J.P.R.", "symbols"),
  // traps: contain marker/joiner-looking letters but must never split
  artist("featurecast", "Featurecast", "trap"),
  artist("ftampa", "Ftampa", "trap"),
  artist("andy-c", "Andy C", "trap"),
  artist("feed-me", "Feed Me", "trap"),
  artist("the-xx", "The xx", "trap"),
  artist("malcolm-x", "Malcolm X", "trap"),
  artist("jay-z", "Jay-Z", "trap"),
  artist("blink-182", "Blink-182", "trap"),
  artist("mumford-sons", "Mumford & Sons", "trap", { guarded: true }),
  artist("withered-hand", "Withered Hand", "trap"),
  artist("xample", "Xample", "trap"),
  artist("the-prodigy", "The Prodigy", "trap"),
  artist("andromedik", "Andromedik", "trap"),
  // numerics (careful with R4)
  artist("2-unlimited", "2 Unlimited", "numeric"),
  artist("808-state", "808 State", "numeric"),
  artist("50-cent", "50 Cent", "numeric"),
  artist("21-savage", "21 Savage", "numeric"),
  artist("m83", "M83", "numeric"),
  artist("311", "311", "numeric"),
  artist("112", "112", "numeric"),
];

export const artistById = (id: string): ArtistAtom => byId(ARTISTS, id);

// ---------------------------------------------------------------- titles

export interface TitleAtom extends Atom {
  text: string;
  cat: "plain" | "cyrillic" | "diacritics" | "cjk" | "apostrophe" | "numeric" | "symbols" | "group";
}

function title(id: string, text: string, cat: TitleAtom["cat"]): TitleAtom {
  return { id, text, cat, tags: [`title-${cat}`] };
}

export const TITLES: TitleAtom[] = [
  title("diplodocus", "Diplodocus", "plain"),
  title("bruises", "Bruises", "plain"),
  title("stigma", "Stigma", "plain"),
  title("hologram", "Hologram", "plain"),
  title("levitate", "Levitate", "plain"),
  title("firestarter", "Firestarter", "plain"),
  title("numb", "Numb", "plain"),
  title("one-more-time", "One More Time", "plain"),
  title("around-the-world", "Around the World", "plain"),
  title("hey-jude", "Hey Jude", "plain"),
  title("strobe", "Strobe", "plain"),
  title("machine-gun", "Machine Gun", "plain"),
  title("midnight-city", "Midnight City", "plain"),
  title("blue-monday", "Blue Monday", "plain"),
  title("gruppa-krovi", "Группа крови", "cyrillic"),
  title("kukla-kolduna", "Кукла колдуна", "cyrillic"),
  title("vyhoda-net", "Выхода нет", "cyrillic"),
  title("hoppipolla", "Hoppípolla", "diacritics"),
  title("cafe-del-mar", "Café del Mar", "diacritics"),
  title("deja-vu", "Déjà Vu", "diacritics"),
  title("facade", "Façade", "diacritics"),
  title("hanataba", "花束を君に", "cjk"),
  title("bomnal", "봄날", "cjk"),
  title("yoru-ni-kakeru", "夜に駆ける", "cjk"),
  title("dont-stop-me-now", "Don't Stop Me Now", "apostrophe"),
  title("im-not-alright", "I'm Not Alright", "apostrophe"),
  title("rock-n-roll-star", "Rock 'n' Roll Star", "apostrophe"),
  title("party-like-1999", "Party Like It's 1999", "numeric"),
  title("99-problems", "99 Problems", "numeric"),
  title("7-rings", "7 Rings", "numeric"),
  title("up-tempo", "Up-Tempo", "symbols"),
  title("stand-by-me", "Stand by Me", "symbols"),
  title("spy-vs-spy", "Spy vs Spy", "symbols"),
  title("cat-x-mouse", "Cat x Mouse", "symbols"),
  title("salt-and-pepper", "Salt & Pepper", "symbols"),
  title("mr-brightside", "Mr. Brightside", "symbols"),
  title("whats-up", "What's Up?", "symbols"),
  title("satisfaction", "(I Can't Get No) Satisfaction", "group"),
  title("song-part-2", "Song (Part 2)", "group"),
  title("the-reaper", "(Don't Fear) The Reaper", "group"),
  title("voices-part-ii", "Voices [Part II]", "group"),
];

export const titleById = (id: string): TitleAtom => byId(TITLES, id);

// ---------------------------------------------------------------- joiners

export type JoinerKind = "split" | "and" | "with" | "nosplit";

export interface JoinerAtom extends Atom {
  /** Exact text placed between two names. */
  render: string;
  /** Expected `joiner` value (trimmed, case kept). */
  raw: string;
  kind: JoinerKind;
  /** Tight joiner (`,` `;`). */
  tight?: boolean;
  /** Contains a character unsafe in filenames. */
  fileUnsafe?: boolean;
}

function joiner(id: string, render: string, kind: JoinerKind = "split", extra: Partial<JoinerAtom> = {}): JoinerAtom {
  return { id, render, raw: render.trim(), kind, ...extra };
}

export const JOINERS: JoinerAtom[] = [
  joiner("amp", " & "),
  joiner("and", " and ", "and"),
  joiner("and-upper", " AND ", "and"),
  joiner("x", " x "),
  joiner("x-upper", " X ", "nosplit"),
  joiner("times", " × "),
  joiner("vs", " vs "),
  joiner("vs-dot", " vs. "),
  joiner("vs-dot-cap", " Vs. "),
  joiner("versus", " versus "),
  joiner("slash", " / ", "split", { fileUnsafe: true }),
  joiner("plus", " + "),
  joiner("with", " with ", "with"),
  joiner("meets", " meets "),
  joiner("pres", " pres. "),
  joiner("presents", " presents "),
  joiner("b2b", " b2b "),
  joiner("b2b-upper", " B2B "),
  joiner("comma", ", ", "split", { tight: true }),
  joiner("comma-tight", ",", "split", { tight: true }),
  joiner("comma-spaced", " , ", "split", { tight: true }),
  joiner("semicolon", "; ", "split", { tight: true }),
];

export const joinerById = (id: string): JoinerAtom => byId(JOINERS, id);

// ---------------------------------------------------------------- feat / producer markers

export interface FeatMarkerAtom extends Atom {
  text: string;
  bracketOnly: boolean;
  /** Recognised unbracketed on the title side (R8.3: only feat., ft., featuring). */
  titleInline: boolean;
}

const fm = (id: string, text: string, bracketOnly: boolean, titleInline: boolean): FeatMarkerAtom => ({
  id,
  text,
  bracketOnly,
  titleInline,
});

export const FEAT_MARKERS: FeatMarkerAtom[] = [
  fm("ft-dot", "ft.", false, true),
  fm("ft", "ft", false, false),
  fm("feat-dot", "feat.", false, true),
  fm("feat", "feat", false, false),
  fm("featuring", "featuring", false, true),
  fm("feat-dot-cap", "Feat.", false, true),
  fm("ft-dot-upper", "FT.", false, true),
  fm("featuring-cap", "Featuring", false, true),
  fm("feat-colon", "feat:", false, false),
  fm("with", "with", true, false),
  fm("with-cap", "With", true, false),
  fm("w-slash", "w/", true, false),
];

export const featMarkerById = (id: string): FeatMarkerAtom => byId(FEAT_MARKERS, id);

export type FeatPlacement =
  | "artist-inline"
  | "artist-paren"
  | "artist-square"
  | "title-paren"
  | "title-square"
  | "title-curly"
  | "title-inline";

export const FEAT_PLACEMENTS: (Atom & { placement: FeatPlacement })[] = [
  { id: "artist-inline", placement: "artist-inline" },
  { id: "artist-paren", placement: "artist-paren" },
  { id: "artist-square", placement: "artist-square" },
  { id: "title-paren", placement: "title-paren" },
  { id: "title-square", placement: "title-square" },
  { id: "title-curly", placement: "title-curly" },
  { id: "title-inline", placement: "title-inline" },
];

export interface ProducerMarkerAtom extends Atom {
  text: string;
  /** Allowed unbracketed in the title (R8.3: prod., prod. by, produced by, prod.by). */
  inline: boolean;
}

export const PRODUCER_MARKERS: ProducerMarkerAtom[] = [
  { id: "prod-dot", text: "prod.", inline: true },
  { id: "prod-dot-cap", text: "Prod.", inline: true },
  { id: "prod-by", text: "prod. by", inline: true },
  { id: "prod-by-cap", text: "Prod. by", inline: true },
  { id: "produced-by", text: "produced by", inline: true },
  { id: "prod", text: "prod", inline: false },
  { id: "prodby-tight", text: "prod.by", inline: true },
];

export const producerMarkerById = (id: string): ProducerMarkerAtom => byId(PRODUCER_MARKERS, id);

// ---------------------------------------------------------------- versions

export type VersionType =
  | "remix" | "bootleg" | "vip" | "edit" | "flip" | "refix" | "rework" | "mashup" | "blend" | "dub" | "mix"
  | "extended" | "radio" | "club" | "original" | "instrumental" | "acapella" | "live" | "acoustic"
  | "remaster" | "demo" | "reprise" | "cover" | "version" | "spedUp" | "slowed" | "nightcore";

export interface VersionPart {
  /** Template; `{R}` is replaced by a rendered remixer credit list. */
  tpl: string;
  type: VersionType;
  modifiers: string[];
  descriptor: string | null;
  year: number | null;
  unknownArtist: boolean;
  /**
   * R5.7.2: the credit span goes to `descriptor` (type version/cover), so `{R}` yields no
   * remixers and the rendered credit becomes the descriptor.
   */
  creditAsDescriptor?: boolean;
  /** Recognised by the R5.7.3 prefix form. */
  prefixForm?: boolean;
}

export interface VersionAtom extends Atom {
  /** One part per resulting version (several = R5.6 multi-version group). */
  parts: VersionPart[];
  /** Rule ids this atom exercises. */
  rules: string[];
  /** Peelable as a dash suffix even without an artist side (R6.2 ii/iii; ii not for prefix forms). */
  peelAlone: boolean;
}

function v(
  id: string,
  tpl: string,
  type: VersionType,
  rules: string[],
  extra: Partial<Omit<VersionPart, "tpl" | "type">> = {},
): VersionAtom {
  const part: VersionPart = {
    tpl,
    type,
    modifiers: extra.modifiers ?? [],
    descriptor: extra.descriptor ?? null,
    year: extra.year ?? null,
    unknownArtist: extra.unknownArtist ?? false,
  };
  if (extra.creditAsDescriptor) part.creditAsDescriptor = true;
  if (extra.prefixForm) part.prefixForm = true;
  const wordsNoSlot = tpl.split(" ").length;
  const peelAlone = (wordsNoSlot >= 2 && !part.prefixForm) || part.year !== null;
  return { id, parts: [part], rules, peelAlone, tags: [`v-${type}`] };
}

const HS = "R5.7"; // head scan / head-by / prefix are all R5.7

export const VERSIONS: VersionAtom[] = [
  // credited, head scan
  v("remix", "{R} Remix", "remix", [HS]),
  v("remix-lower", "{R} remix", "remix", [HS, "R0.3"]),
  v("remix-upper", "{R} REMIX", "remix", [HS, "R0.3"]),
  v("rmx", "{R} Rmx", "remix", [HS]),
  v("vip", "{R} VIP", "vip", [HS]),
  v("vip-mix", "{R} VIP Mix", "vip", [HS]),
  v("bootleg", "{R} Bootleg", "bootleg", [HS]),
  v("flip", "{R} Flip", "flip", [HS]),
  v("refix", "{R} Refix", "refix", [HS]),
  v("rework", "{R} Rework", "rework", [HS]),
  v("re-edit", "{R} Re-Edit", "edit", [HS]),
  v("edit", "{R} Edit", "edit", [HS]),
  v("mix", "{R} Mix", "remix", [HS]),
  v("dub", "{R} Dub", "dub", [HS]),
  v("mashup", "{R} Mashup", "mashup", [HS]),
  v("blend", "{R} Blend", "blend", [HS]),
  v("cover", "{R} Cover", "cover", [HS], { creditAsDescriptor: true }),
  v("version", "{R} Version", "version", [HS], { creditAsDescriptor: true }),
  v("vip-remix", "{R} VIP Remix", "remix", [HS], { modifiers: ["vip"] }),
  v("extended-mix-credit", "{R} Extended Mix", "extended", [HS]),
  v("extended-vip-credit", "{R} Extended VIP Mix", "vip", [HS], { modifiers: ["extended"] }),
  v("possessive", "{R}'s Remix", "remix", [HS]),
  v("year-remix", "{R} 2015 Remix", "remix", [HS], { year: 2015 }),
  v("dnb-remix", "{R} D'n'B Remix", "remix", [HS], { descriptor: "D'n'B" }),
  v("neurofunk-remix", "{R} Neurofunk Remix", "remix", [HS], { descriptor: "Neurofunk" }),
  v("remix-by", "Remix by {R}", "remix", [HS]),
  // uncredited
  v("id-remix", "ID Remix", "remix", [HS, "R7.5"], { unknownArtist: true }),
  v("radio-edit", "Radio Edit", "radio", [HS]),
  v("extended-mix", "Extended Mix", "extended", [HS]),
  v("original-mix", "Original Mix", "original", [HS]),
  v("vip-mix-bare", "VIP Mix", "vip", [HS]),
  v("vip-bare", "VIP", "vip", [HS]),
  v("dub-mix", "Dub Mix", "dub", [HS]),
  v("club-mix", "Club Mix", "club", [HS]),
  v("extended-club-mix", "Extended Club Mix", "club", [HS], { modifiers: ["extended"] }),
  v("extended-vip-mix", "Extended VIP Mix", "vip", [HS], { modifiers: ["extended"] }),
  v("acoustic-version", "Acoustic Version", "acoustic", [HS]),
  v("club-ver", "Club Ver", "club", [HS]),
  v("vip-remix-bare", "VIP Remix", "remix", [HS], { modifiers: ["vip"] }),
  v("remastered-version", "2011 Remastered Version", "remaster", [HS], { year: 2011 }),
  v("instrumental", "Instrumental", "instrumental", [HS], { prefixForm: true }),
  v("acoustic", "Acoustic", "acoustic", [HS], { prefixForm: true }),
  v("live", "Live", "live", [HS], { prefixForm: true }),
  v("live-at-wembley", "Live at Wembley 1986", "live", [HS], {
    year: 1986,
    descriptor: "at Wembley",
    prefixForm: true,
  }),
  v("remastered", "Remastered", "remaster", [HS]),
  v("remastered-2015", "Remastered 2015", "remaster", [HS], { year: 2015, prefixForm: true }),
  v("remaster-2011", "2011 Remaster", "remaster", [HS], { year: 2011 }),
  v("sped-up", "Sped Up", "spedUp", [HS], { prefixForm: true }),
  v("slowed-reverb", "Slowed + Reverb", "slowed", [HS], { prefixForm: true }),
  v("nightcore", "Nightcore", "nightcore", [HS], { prefixForm: true }),
  v("demo", "Demo", "demo", [HS], { prefixForm: true }),
  v("reprise", "Reprise", "reprise", [HS]),
  v("remix-bare", "Remix", "remix", [HS]),
  v("edit-bare", "Edit", "edit", [HS]),
  v("mix-bare", "Mix", "mix", [HS]),
  v("taylors-version", "Taylor's Version", "version", [HS], { descriptor: "Taylor's" }),
  v("japanese-version", "Japanese Version", "version", [HS], { descriptor: "Japanese" }),
];

/** R5.6 multi-version groups: two credited parts joined by ` / ` or `; `. */
export const MULTI_VERSIONS: VersionAtom[] = [
  {
    id: "multi-remix-vip",
    parts: [
      { tpl: "{R} Remix", type: "remix", modifiers: [], descriptor: null, year: null, unknownArtist: false },
      { tpl: "{R} VIP", type: "vip", modifiers: [], descriptor: null, year: null, unknownArtist: false },
    ],
    rules: ["R5.6", HS],
    peelAlone: true,
    tags: ["v-multi"],
  },
  {
    id: "multi-radio-extended",
    parts: [
      { tpl: "Radio Edit", type: "radio", modifiers: [], descriptor: null, year: null, unknownArtist: false },
      { tpl: "Extended Mix", type: "extended", modifiers: [], descriptor: null, year: null, unknownArtist: false },
    ],
    rules: ["R5.6", HS],
    peelAlone: true,
    tags: ["v-multi"],
  },
];

export const ALL_VERSIONS = [...VERSIONS, ...MULTI_VERSIONS];
export const versionById = (id: string): VersionAtom => byId(ALL_VERSIONS, id);
export const slotCount = (va: VersionAtom): number => va.parts.filter((p) => p.tpl.includes("{R}")).length;

export type Delim = "(" | "[" | "{" | "-";
export const DELIMS: (Atom & { delim: Delim })[] = [
  { id: "paren", delim: "(" },
  { id: "square", delim: "[" },
  { id: "curly", delim: "{" },
  { id: "dash", delim: "-" },
];

// ---------------------------------------------------------------- extras (year / flag / unknown groups)

export interface ExtraAtom extends Atom {
  text: string;
  kind: "year" | "explicit" | "clean" | "unknown";
  year?: number;
  rules: string[];
}

export const EXTRAS: ExtraAtom[] = [
  { id: "year-paren", text: "(2015)", kind: "year", year: 2015, rules: ["R5.5"] },
  { id: "year-square", text: "[1999]", kind: "year", year: 1999, rules: ["R5.5"] },
  { id: "explicit", text: "(Explicit)", kind: "explicit", rules: ["R5.4"] },
  { id: "dirty", text: "[Dirty]", kind: "explicit", rules: ["R5.4"] },
  { id: "explicit-version", text: "(Explicit Version)", kind: "explicit", rules: ["R5.4"] },
  { id: "clean-version", text: "(Clean Version)", kind: "clean", rules: ["R5.4"] },
  { id: "radio-clean", text: "[Radio Clean]", kind: "clean", rules: ["R5.4"] },
  { id: "pt-1", text: "(Pt. 1)", kind: "unknown", rules: ["R5.8"] },
  { id: "bonus-track", text: "[Bonus Track]", kind: "unknown", rules: ["R5.8"] },
  { id: "moving-shadow", text: "(Moving Shadow 2003)", kind: "unknown", rules: ["R5.8"] },
];

export const extraById = (id: string): ExtraAtom => byId(EXTRAS, id);

// ---------------------------------------------------------------- junk

export type JunkKind = "video" | "audio" | "lyrics" | "quality" | "promo" | "platform" | "label" | "genre" | "other";
export type JunkForm = "paren" | "square" | "dash" | "trailing" | "pipe";

export interface JunkAtom extends Atom {
  text: string;
  kind: JunkKind;
  forms: JunkForm[];
}

const BR: JunkForm[] = ["paren", "square"];
const BRD: JunkForm[] = ["paren", "square", "dash", "pipe"];
const ALLF: JunkForm[] = ["paren", "square", "dash", "pipe", "trailing"];

function junk(id: string, text: string, kind: JunkKind, forms: JunkForm[]): JunkAtom {
  return { id, text, kind, forms, tags: [`junk-${kind}`] };
}

export const JUNK: JunkAtom[] = [
  junk("official-video", "Official Video", "video", BRD),
  junk("official-music-video", "Official Music Video", "video", BRD),
  junk("official-visualizer", "Official Visualizer", "video", BRD),
  junk("official-video-hd", "Official Video HD", "video", BRD),
  junk("official-video-pipe-hd", "Official Video | HD", "video", BR),
  junk("official-video-2015", "Official Video 2015", "video", BRD),
  junk("official-audio", "Official Audio", "audio", BRD),
  junk("audio", "Audio", "audio", ALLF),
  junk("lyrics", "Lyrics", "lyrics", ALLF),
  junk("lyric-video", "Lyric Video", "lyrics", BRD),
  junk("official-lyric-video", "Official Lyric Video", "lyrics", BRD),
  junk("letra", "Letra", "lyrics", ALLF),
  junk("hd", "HD", "quality", ALLF),
  junk("4k", "4K", "quality", ALLF),
  junk("hq", "HQ", "quality", ALLF),
  junk("1080p", "1080p", "quality", ALLF),
  junk("free-download", "Free Download", "promo", ALLF),
  junk("out-now", "Out Now", "promo", ALLF),
  junk("premiere", "Premiere", "promo", ALLF),
  junk("ncs-release", "NCS Release", "label", BRD),
  junk("monstercat-release", "Monstercat Release", "label", BRD),
  junk("hospital-records", "Hospital Records", "label", BR),
  junk("critical-recordings", "Critical Music Recordings", "label", BR),
  junk("dubstep", "Dubstep", "genre", BR),
  junk("drum-and-bass", "Drum & Bass", "genre", BR),
  junk("official", "Official", "other", ["paren", "square", "dash"]),
  // channel names: only as a youtube pipe segment → junk kind "other"
  junk("ch-monstercat", "Monstercat", "other", ["pipe"]),
  junk("ch-ukf", "UKF", "other", ["pipe"]),
  junk("ch-liquicity", "Liquicity", "other", ["pipe"]),
  junk("ch-trap-nation", "Trap Nation", "other", ["pipe"]),
  junk("ch-proximity", "Proximity", "other", ["pipe"]),
];

export const junkById = (id: string): JunkAtom => byId(JUNK, id);
/** A pipe segment that is NOT junk vocabulary (becomes kind other via R2.4.2). */
export const isChannel = (j: JunkAtom): boolean => j.id.startsWith("ch-");

export const JUNK_FORMS: (Atom & { form: JunkForm })[] = [
  { id: "paren", form: "paren" },
  { id: "square", form: "square" },
  { id: "dash", form: "dash" },
  { id: "trailing", form: "trailing" },
  { id: "pipe", form: "pipe" },
];

// ---------------------------------------------------------------- separators

export type SepKind = "spaced" | "pipe" | "unspaced" | "asym";

export interface SepAtom extends Atom {
  text: string;
  kind: SepKind;
  rules: string[];
}

export const SEPARATORS: SepAtom[] = [
  { id: "hyphen", text: " - ", kind: "spaced", rules: ["R6.1", "R6.3"] },
  { id: "en-dash", text: " \u2013 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "em-dash", text: " \u2014 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "double-hyphen", text: " -- ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "minus", text: " \u2212 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "hyphen-2010", text: " \u2010 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "figure-dash", text: " \u2012 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "horizontal-bar", text: " \u2015 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "small-hyphen", text: " \uFE63 ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "fullwidth-hyphen", text: " \uFF0D ", kind: "spaced", rules: ["R1.5", "R6.1", "R6.3"] },
  { id: "pipe", text: " | ", kind: "pipe", rules: ["R2.4"] },
  { id: "double-pipe", text: " || ", kind: "pipe", rules: ["R2.4"] },
  { id: "unspaced", text: "-", kind: "unspaced", rules: ["R6.7"] },
  { id: "unspaced-en", text: "\u2013", kind: "unspaced", rules: ["R1.5", "R6.7"] },
  { id: "asym-left", text: " -", kind: "asym", rules: ["R6.4"] },
  { id: "asym-right", text: "- ", kind: "asym", rules: ["R6.4"] },
];

export const sepById = (id: string): SepAtom => byId(SEPARATORS, id);
export const SPACED_SEPS = SEPARATORS.filter((s) => s.kind === "spaced");

// ---------------------------------------------------------------- prefixes

export interface PrefixAtom extends Atom {
  text: string;
  position: { raw: string; number: number } | null;
  timestamp: { raw: string; seconds: number } | null;
  /** R4.2 form of the position part. */
  form: "a" | "b" | "c" | "e" | null;
  rules: string[];
}

function pfx(
  id: string,
  text: string,
  position: PrefixAtom["position"],
  timestamp: PrefixAtom["timestamp"],
  form: PrefixAtom["form"],
): PrefixAtom {
  const rules: string[] = [];
  if (timestamp) rules.push("R4.1");
  if (position) rules.push("R4.2");
  return { id, text, position, timestamp, form, rules, tags: [position ? "position" : "", timestamp ? "timestamp" : ""].filter(Boolean) };
}

export const PREFIXES: PrefixAtom[] = [
  pfx("pos-01-dot", "01. ", { raw: "01", number: 1 }, null, "a"),
  pfx("pos-1-dot", "1. ", { raw: "1", number: 1 }, null, "a"),
  pfx("pos-12-paren", "12) ", { raw: "12", number: 12 }, null, "a"),
  pfx("pos-003-dot", "003. ", { raw: "003", number: 3 }, null, "a"),
  pfx("pos-01-space", "01 ", { raw: "01", number: 1 }, null, "b"),
  pfx("pos-07-dash", "07 - ", { raw: "07", number: 7 }, null, "b"),
  pfx("pos-3-dash", "3 - ", { raw: "3", number: 3 }, null, "c"),
  pfx("pos-10-dash", "10 - ", { raw: "10", number: 10 }, null, "c"),
  pfx("ts-square", "[00:00] ", null, { raw: "00:00", seconds: 0 }, null),
  pfx("ts-paren", "(01:23) ", null, { raw: "01:23", seconds: 83 }, null),
  pfx("ts-bare", "0:45 ", null, { raw: "0:45", seconds: 45 }, null),
  pfx("ts-hms", "1:02:03 ", null, { raw: "1:02:03", seconds: 3723 }, null),
  pfx("ts-dash", "12:34 - ", null, { raw: "12:34", seconds: 754 }, null),
  pfx("ts-square-hms", "[01:00:00] ", null, { raw: "01:00:00", seconds: 3600 }, null),
  pfx("ts-pos-a", "[00:00] 01. ", { raw: "01", number: 1 }, { raw: "00:00", seconds: 0 }, "a"),
  pfx("ts-pos-b", "(02:30) 02 ", { raw: "02", number: 2 }, { raw: "02:30", seconds: 150 }, "b"),
  pfx("file-01-hyphen", "01-", { raw: "01", number: 1 }, null, "e"),
  pfx("file-01-underscore", "01_", { raw: "01", number: 1 }, null, "e"),
  pfx("file-01-dot", "01.", { raw: "01", number: 1 }, null, "e"),
];

export const prefixById = (id: string): PrefixAtom => byId(PREFIXES, id);

// ---------------------------------------------------------------- modes

export interface PlatformAtom extends Atom {
  text: string;
  raw: string;
}

export const PLATFORMS: PlatformAtom[] = [
  { id: "youtube", text: " - YouTube", raw: "YouTube" },
  { id: "youtube-music", text: " - YouTube Music", raw: "YouTube Music" },
  { id: "soundcloud-dash", text: " - SoundCloud", raw: "SoundCloud" },
  { id: "soundcloud-pipe", text: " | SoundCloud", raw: "SoundCloud" },
  { id: "bandcamp-pipe", text: " | Bandcamp", raw: "Bandcamp" },
  { id: "topic", text: " - Topic", raw: "Topic" },
];

export interface ModeAtom extends Atom {
  option: "clean" | "youtube" | "filename" | "auto";
  ext?: string;
  underscore?: boolean;
  platform?: PlatformAtom;
}

const platform = (id: string): PlatformAtom => byId(PLATFORMS, id);

export const MODES: ModeAtom[] = [
  { id: "clean", option: "clean" },
  { id: "youtube", option: "youtube" },
  { id: "youtube-yt", option: "youtube", platform: platform("youtube") },
  { id: "youtube-ytm", option: "youtube", platform: platform("youtube-music") },
  { id: "youtube-sc", option: "youtube", platform: platform("soundcloud-pipe") },
  { id: "youtube-sc-dash", option: "youtube", platform: platform("soundcloud-dash") },
  { id: "youtube-bandcamp", option: "youtube", platform: platform("bandcamp-pipe") },
  { id: "youtube-topic", option: "youtube", platform: platform("topic") },
  { id: "filename-mp3", option: "filename", ext: "mp3" },
  { id: "filename-flac", option: "filename", ext: "flac" },
  { id: "filename-m4a", option: "filename", ext: "m4a" },
  { id: "filename-opus", option: "filename", ext: "opus" },
  { id: "filename-wav", option: "filename", ext: "wav" },
  { id: "filename-webm", option: "filename", ext: "webm" },
  { id: "filename-mp3-us", option: "filename", ext: "mp3", underscore: true },
  { id: "filename-flac-us", option: "filename", ext: "flac", underscore: true },
  { id: "auto", option: "auto" },
  { id: "auto-yt", option: "auto", platform: platform("youtube") },
  { id: "auto-sc", option: "auto", platform: platform("soundcloud-pipe") },
  { id: "auto-mp3", option: "auto", ext: "mp3" },
  { id: "auto-flac", option: "auto", ext: "flac" },
];

export const modeById = (id: string): ModeAtom => byId(MODES, id);

// ---------------------------------------------------------------- unicode transforms

export interface TransformAtom extends Atom {
  apply: (s: string) => string;
  rules: string[];
}

/** Insert `ch` after the first code point of every word with ≥2 code points. */
function insertInWords(ch: string): (s: string) => string {
  return (s) =>
    s
      .split(" ")
      .map((w) => {
        const cps = Array.from(w);
        return cps.length >= 2 ? cps[0] + ch + cps.slice(1).join("") : w;
      })
      .join(" ");
}

export const TRANSFORMS: TransformAtom[] = [
  { id: "none", apply: (s) => s, rules: [] },
  { id: "nfd", apply: (s) => s.normalize("NFD"), rules: ["R1.1"] },
  { id: "zwsp", apply: insertInWords("\u200B"), rules: ["R1.2"] },
  { id: "soft-hyphen", apply: insertInWords("\u00AD"), rules: ["R1.2"] },
  { id: "word-joiner", apply: insertInWords("\u2060"), rules: ["R1.2"] },
  { id: "bom", apply: (s) => "\uFEFF" + s, rules: ["R1.2"] },
  { id: "fullwidth-brackets", apply: (s) => s.replace(/[()[\]{}]/g, (c) => FULLWIDTH[c]!), rules: ["R1.3"] },
  { id: "curly-apostrophe", apply: (s) => s.replace(/'/g, "\u2019"), rules: ["R1.4"] },
  { id: "nbsp", apply: (s) => s.replace(/ /g, "\u00A0"), rules: ["R1.6"] },
  { id: "ideographic-space", apply: (s) => s.replace(/ /g, "\u3000"), rules: ["R1.6"] },
  { id: "thin-space", apply: (s) => s.replace(/ /g, "\u2009"), rules: ["R1.6"] },
  { id: "double-space", apply: (s) => s.replace(/ /g, "  "), rules: ["R1.6"] },
  { id: "padded", apply: (s) => `  ${s} \t`, rules: ["R1.6"] },
];

const FULLWIDTH: Record<string, string> = { "(": "\uFF08", ")": "\uFF09", "[": "【", "]": "】", "{": "\uFF5B", "}": "\uFF5D" };

export const transformById = (id: string): TransformAtom => byId(TRANSFORMS, id);

// ---------------------------------------------------------------- helpers

export function byId<T extends Atom>(pool: readonly T[], id: string): T {
  const found = pool.find((a) => a.id === id);
  if (!found) throw new Error(`unknown atom id ${id}`);
  return found;
}

/** A pseudo-atom meaning "dimension not used". */
export const NONE: Atom = { id: "none" };
