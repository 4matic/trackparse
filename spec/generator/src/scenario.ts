import { readFileSync } from "node:fs";
import { join } from "node:path";
import { type Atom, sepById } from "./atoms.js";
import { type Combo, exhaustive, pairwise, productSize, sample } from "./combinatorics.js";
import { GENERATOR_ROOT } from "./data.js";
import { type AssertOpts, type Composition, type Rendered, problem, render } from "./model.js";
import { Rng, hashString } from "./prng.js";

export interface Config {
  seed: number;
  exhaustiveLimit: number;
  pairwiseCandidates: number;
  kitchenSinkSample: number;
}

export function loadConfig(): Config {
  return JSON.parse(readFileSync(join(GENERATOR_ROOT, "config.json"), "utf8")) as Config;
}

export interface Dim {
  name: string;
  values: readonly Atom[];
}

export type Choice = Record<string, Atom>;

export interface Scenario {
  name: string;
  description: string;
  dims: Dim[];
  /**
   * Map a pick to a composition; return null to reject (scenario-level constraint).
   * Call `ignore(dim)` for dimensions that do not influence this composition: they are
   * shown as `none` in the case id (so equivalent picks collapse to one case).
   */
  build: (pick: Choice, ignore: (dim: string) => void) => Composition | null;
  /** Extra random sample on top of pairwise (ignored when exhaustive). */
  sample: number | ((cfg: Config) => number);
  assert?: AssertOpts;
}

export interface GeneratedCase extends Rendered {
  id: string;
}

export interface ScenarioResult {
  name: string;
  description: string;
  cases: GeneratedCase[];
  stats: {
    strategy: "exhaustive" | "pairwise+sample";
    productSize: number;
    pairwiseCases: number;
    sampledCases: number;
    infeasiblePairs: number;
    duplicateInputsDropped: number;
    dims: Record<string, Record<string, number>>;
  };
}

/** Standard composition skeleton; scenarios override fields. */
export function base(parts: Partial<Composition> & Pick<Composition, "title" | "mode">): Composition {
  return {
    prefix: null,
    main: null,
    feat: null,
    sep: sepById("hyphen"),
    producer: null,
    versions: [],
    extras: [],
    dashYear: null,
    junk: [],
    splitAnd: "auto",
    transform: null,
    ...parts,
  };
}

export function runScenario(s: Scenario, cfg: Config): ScenarioResult {
  const sizes = s.dims.map((d) => d.values.length);
  for (const d of s.dims) {
    const ids = d.values.map((v) => v.id);
    if (new Set(ids).size !== ids.length) throw new Error(`${s.name}: duplicate atom ids in dim ${d.name}`);
    for (const id of ids) if (!/^[a-z0-9][a-z0-9-]*$/.test(id)) throw new Error(`${s.name}: bad atom id ${id}`);
  }
  const cache = new Map<string, { comp: Composition; ignored: Set<string> } | null>();
  const compose = (c: Combo): { comp: Composition; ignored: Set<string> } | null => {
    const key = c.join(",");
    const hit = cache.get(key);
    if (hit !== undefined) return hit;
    const pick: Choice = {};
    s.dims.forEach((d, i) => {
      pick[d.name] = d.values[c[i]!]!;
    });
    const ignored = new Set<string>();
    const comp = s.build(pick, (d) => ignored.add(d));
    const res = comp && problem(comp) === null ? { comp, ignored } : null;
    cache.set(key, res);
    return res;
  };
  const valid = (c: Combo) => compose(c) !== null;
  const rng = new Rng((cfg.seed ^ hashString(s.name)) >>> 0);
  const total = productSize(sizes);

  let combos: Combo[];
  let strategy: ScenarioResult["stats"]["strategy"];
  let pairwiseCases = 0;
  let sampledCases = 0;
  let infeasiblePairs = 0;
  if (total <= cfg.exhaustiveLimit || s.dims.length < 2) {
    strategy = "exhaustive";
    combos = exhaustive(sizes, valid);
  } else {
    strategy = "pairwise+sample";
    const pw = pairwise(sizes, valid, rng, cfg.pairwiseCandidates);
    infeasiblePairs = pw.infeasiblePairs;
    pairwiseCases = pw.combos.length;
    const n = typeof s.sample === "function" ? s.sample(cfg) : s.sample;
    const extra = sample(sizes, valid, rng, n, new Set(pw.combos.map((c) => c.join(","))));
    sampledCases = extra.length;
    combos = [...pw.combos, ...extra];
  }

  const byId = new Map<string, { combo: Combo; rendered: Rendered }>();
  for (const c of combos) {
    const { comp, ignored } = compose(c)!;
    const eff = c.map((v, i) => (ignored.has(s.dims[i]!.name) ? -1 : v));
    const id = `gen:${s.name}:${eff.map((v, i) => (v < 0 ? "none" : s.dims[i]!.values[v]!.id)).join(".")}`;
    if (byId.has(id)) continue;
    byId.set(id, { combo: eff, rendered: render(comp, s.name, s.assert) });
  }
  // Drop combos rendering the same input+options (keep the smallest id).
  const ids = [...byId.keys()].sort();
  const seenInputs = new Set<string>();
  const cases: GeneratedCase[] = [];
  const dimsCount: Record<string, Record<string, number>> = {};
  for (const d of s.dims) dimsCount[d.name] = Object.fromEntries([...d.values.map((v) => [v.id, 0]), ["(n/a)", 0]]);
  let dropped = 0;
  for (const id of ids) {
    const { combo, rendered } = byId.get(id)!;
    const k = `${rendered.input}\u0000${JSON.stringify(rendered.options)}`;
    if (seenInputs.has(k)) {
      dropped++;
      continue;
    }
    seenInputs.add(k);
    cases.push({ id, ...rendered });
    combo.forEach((v, i) => {
      const d = s.dims[i]!;
      dimsCount[d.name]![v < 0 ? "(n/a)" : d.values[v]!.id]!++;
    });
  }
  for (const d of Object.values(dimsCount)) if (d["(n/a)"] === 0) delete d["(n/a)"];
  return {
    name: s.name,
    description: s.description,
    cases,
    stats: {
      strategy,
      productSize: total,
      pairwiseCases,
      sampledCases,
      infeasiblePairs,
      duplicateInputsDropped: dropped,
      dims: dimsCount,
    },
  };
}
