import type { Scenario } from "../scenario.js";
import { artistsJoiners } from "./artists-joiners.js";
import { dashSuffix } from "./dash-suffix.js";
import { dedupScenario } from "./dedup.js";
import { featPlacement } from "./feat-placement.js";
import { filename } from "./filename.js";
import { junkYoutube } from "./junk-youtube.js";
import { kitchenSink } from "./kitchen-sink.js";
import { prefixes } from "./prefixes.js";
import { unicode } from "./unicode.js";
import { versions } from "./versions.js";

export const SCENARIOS: Scenario[] = [
  artistsJoiners,
  featPlacement,
  versions,
  prefixes,
  junkYoutube,
  filename,
  unicode,
  dashSuffix,
  dedupScenario,
  kitchenSink,
];
