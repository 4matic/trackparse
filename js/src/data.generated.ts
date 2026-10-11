// GENERATED from spec/data — do not edit. Run `pnpm gen:data` to regenerate.
/* eslint-disable */

export const SPEC_VERSION = "0.2.1"; // x-release-please-version

/** spec/data/descriptors.json */
export const descriptorsData = {
  "descriptors": [
    "radio",
    "extended",
    "club",
    "original",
    "instrumental",
    "dub",
    "vip",
    "acoustic",
    "live",
    "demo",
    "vocal",
    "clean",
    "dirty",
    "explicit",
    "short",
    "long",
    "full",
    "album",
    "single",
    "alternate",
    "alt",
    "alternative",
    "main",
    "special",
    "bonus",
    "deluxe",
    "mono",
    "stereo",
    "new",
    "official",
    "orchestral",
    "piano",
    "unplugged",
    "12\"",
    "7\"",
    "12''",
    "7''",
    "12 inch",
    "7 inch",
    "re",
    "final",
    "second",
    "2nd",
    "3rd"
  ]
} as const;

/** spec/data/extensions.json */
export const extensionsData = {
  "extensions": [
    "mp3",
    "flac",
    "wav",
    "m4a",
    "ogg",
    "opus",
    "aac",
    "aiff",
    "aif",
    "wma",
    "alac",
    "mp4",
    "webm",
    "mkv"
  ]
} as const;

/** spec/data/feat-markers.json */
export const featMarkersData = {
  "markers": [
    "feat.",
    "feat",
    "ft.",
    "ft",
    "featuring",
    "feat:",
    "ft:"
  ],
  "bracketOnly": [
    "with",
    "w/"
  ],
  "producer": [
    "prod.",
    "prod",
    "produced",
    "prod.by",
    "prod:"
  ],
  "titleMarkers": [
    "feat.",
    "ft.",
    "featuring"
  ],
  "titleProducer": [
    "prod.",
    "prod. by",
    "produced by",
    "prod.by"
  ],
  "producerBy": "by"
} as const;

/** spec/data/genres.json */
export const genresData = {
  "genres": [
    "d'n'b",
    "dnb",
    "d&b",
    "d & b",
    "drum & bass",
    "drum&bass",
    "drum and bass",
    "drum n bass",
    "drum 'n' bass",
    "drum'n'bass",
    "neurofunk",
    "neuro",
    "liquid",
    "liquid funk",
    "jump up",
    "jump-up",
    "jungle",
    "halftime",
    "dancefloor",
    "dubstep",
    "riddim",
    "brostep",
    "trap",
    "future bass",
    "house",
    "deep house",
    "tech house",
    "progressive house",
    "electro house",
    "techno",
    "trance",
    "psytrance",
    "psy",
    "hardstyle",
    "hardcore",
    "breaks",
    "breakbeat",
    "garage",
    "uk garage",
    "ukg",
    "2step",
    "2-step",
    "grime",
    "bass",
    "bass house",
    "edm",
    "electronic",
    "idm",
    "synthwave",
    "phonk",
    "hip hop",
    "hip-hop",
    "rap",
    "pop",
    "rock",
    "metal",
    "ambient",
    "chillout",
    "lofi",
    "lo-fi"
  ]
} as const;

/** spec/data/joiners.json */
export const joinersData = {
  "spaced": [
    {
      "raw": "&",
      "canonical": "&"
    },
    {
      "raw": "and",
      "canonical": "&",
      "kind": "and"
    },
    {
      "raw": "x",
      "canonical": "x",
      "caseSensitive": true
    },
    {
      "raw": "×",
      "canonical": "x"
    },
    {
      "raw": "vs.",
      "canonical": "vs."
    },
    {
      "raw": "vs",
      "canonical": "vs."
    },
    {
      "raw": "versus",
      "canonical": "vs."
    },
    {
      "raw": "/",
      "canonical": "/"
    },
    {
      "raw": "+",
      "canonical": "+"
    },
    {
      "raw": "with",
      "canonical": "with"
    },
    {
      "raw": "meets",
      "canonical": "meets"
    },
    {
      "raw": "pres.",
      "canonical": "pres."
    },
    {
      "raw": "presents",
      "canonical": "pres."
    },
    {
      "raw": "b2b",
      "canonical": "b2b"
    }
  ],
  "tight": [
    {
      "raw": ",",
      "canonical": ","
    },
    {
      "raw": ";",
      "canonical": ","
    }
  ],
  "featCanonical": "feat."
} as const;

/** spec/data/junk.json */
export const junkData = {
  "phrases": {
    "official video": "video",
    "official music video": "video",
    "music video": "video",
    "official video clip": "video",
    "video clip": "video",
    "official clip": "video",
    "clip officiel": "video",
    "official hd video": "video",
    "official 4k video": "video",
    "official mv": "video",
    "mv": "video",
    "m/v": "video",
    "video": "video",
    "official visualizer": "video",
    "official visualiser": "video",
    "visualizer": "video",
    "visualiser": "video",
    "audio visualizer": "video",
    "official animated video": "video",
    "animated video": "video",
    "live video": "video",
    "official live video": "video",
    "performance video": "video",
    "official performance video": "video",
    "official audio": "audio",
    "audio": "audio",
    "audio only": "audio",
    "full audio": "audio",
    "official lyric video": "lyrics",
    "official lyrics video": "lyrics",
    "lyric video": "lyrics",
    "lyrics video": "lyrics",
    "lyrics": "lyrics",
    "lyric": "lyrics",
    "with lyrics": "lyrics",
    "lyrics on screen": "lyrics",
    "letra": "lyrics",
    "hd": "quality",
    "hq": "quality",
    "uhd": "quality",
    "4k": "quality",
    "8k": "quality",
    "1080p": "quality",
    "720p": "quality",
    "480p": "quality",
    "high quality": "quality",
    "full hd": "quality",
    "free download": "promo",
    "free dl": "promo",
    "out now": "promo",
    "premiere": "promo",
    "exclusive": "promo",
    "preview": "promo",
    "teaser": "promo",
    "snippet": "promo",
    "new song": "promo",
    "available now": "promo",
    "ncs release": "label",
    "monstercat release": "label",
    "official": "other"
  },
  "connectors": [
    "-",
    "|",
    "/",
    "&",
    "+",
    ","
  ],
  "labelSuffixes": [
    "release",
    "records",
    "recordings"
  ]
} as const;

/** spec/data/no-split-before.json */
export const noSplitBeforeData = {
  "words": [
    "sons",
    "daughters",
    "friends",
    "co",
    "co.",
    "company",
    "his",
    "her",
    "their",
    "orchestra"
  ]
} as const;

/** spec/data/platform-suffixes.json */
export const platformSuffixesData = {
  "suffixes": [
    " - YouTube Music",
    " - YouTube",
    " | Free Listening on SoundCloud",
    " - SoundCloud",
    " | SoundCloud",
    " - Topic",
    " | Bandcamp",
    " - Bandcamp"
  ]
} as const;

/** spec/data/stopwords.json */
export const stopwordsData = {
  "words": [
    "you",
    "me",
    "him",
    "her",
    "us",
    "them",
    "it",
    "i",
    "love",
    "my",
    "your",
    "our",
    "the",
    "a",
    "an",
    "this",
    "that",
    "myself",
    "yourself",
    "everyone",
    "everybody",
    "somebody",
    "someone",
    "nobody",
    "nothing",
    "all",
    "night",
    "side",
    "day"
  ]
} as const;

/** spec/data/unknown-tokens.json */
export const unknownTokensData = {
  "caseSensitive": [
    "ID",
    "IDs",
    "ID?"
  ],
  "caseInsensitive": [
    "unknown",
    "unknown artist",
    "unknown title",
    "untitled",
    "?",
    "??",
    "???"
  ]
} as const;

/** spec/data/version-keywords.json */
export const versionKeywordsData = {
  "heads": {
    "remix": "remix",
    "rmx": "remix",
    "mix": "mix",
    "bootleg": "bootleg",
    "vip": "vip",
    "edit": "edit",
    "re-edit": "edit",
    "reedit": "edit",
    "flip": "flip",
    "refix": "refix",
    "rework": "rework",
    "re-work": "rework",
    "mashup": "mashup",
    "mash-up": "mashup",
    "mash up": "mashup",
    "blend": "blend",
    "dub": "dub",
    "version": "version",
    "ver": "version",
    "remaster": "remaster",
    "remastered": "remaster",
    "cover": "cover",
    "reprise": "reprise"
  },
  "genericHeads": [
    "mix",
    "edit",
    "version"
  ],
  "typeCapableModifiers": {
    "radio": "radio",
    "extended": "extended",
    "club": "club",
    "original": "original",
    "instrumental": "instrumental",
    "dub": "dub",
    "vip": "vip",
    "acoustic": "acoustic",
    "live": "live",
    "acapella": "acapella",
    "a cappella": "acapella",
    "a capella": "acapella",
    "acappella": "acapella",
    "demo": "demo"
  },
  "prefixForms": {
    "live": "live",
    "remastered": "remaster",
    "remaster": "remaster",
    "acoustic": "acoustic",
    "instrumental": "instrumental",
    "acapella": "acapella",
    "a cappella": "acapella",
    "demo": "demo",
    "sped up": "spedUp",
    "speed up": "spedUp",
    "slowed + reverb": "slowed",
    "slowed & reverb": "slowed",
    "slowed and reverb": "slowed",
    "slowed": "slowed",
    "nightcore": "nightcore"
  },
  "types": [
    "remix",
    "bootleg",
    "vip",
    "edit",
    "flip",
    "refix",
    "rework",
    "mashup",
    "blend",
    "dub",
    "mix",
    "extended",
    "radio",
    "club",
    "original",
    "instrumental",
    "acapella",
    "live",
    "acoustic",
    "remaster",
    "demo",
    "reprise",
    "cover",
    "version",
    "spedUp",
    "slowed",
    "nightcore"
  ]
} as const;
