// Copies spec/SPEC.md into the docs as reference/spec.md (git-ignored), so the site always shows
// the spec of the commit it was built from. Relative links are pointed at GitHub.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "../..");
const blob = "https://github.com/4matic/trackparse/blob/main/spec/";
const tree = "https://github.com/4matic/trackparse/tree/main/spec/";

const spec = readFileSync(join(root, "spec/SPEC.md"), "utf8");
const version = readFileSync(join(root, "spec/VERSION"), "utf8").trim();

const body = spec
  // The page title comes from the frontmatter.
  .replace(/^# .*\n+/, "")
  .replace(/^Spec version: .*\n+/m, "")
  // Relative links: files go to blob/, directories (trailing slash or no extension) to tree/.
  .replace(/\]\((?!https?:|#|mailto:)([^)\s]+)\)/g, (_, href) => {
    const clean = href.replace(/^\.\//, "");
    const isDir = clean.endsWith("/") || !/\.[a-z0-9]+(#.*)?$/i.test(clean.split("#")[0]);
    return `](${isDir ? tree : blob}${clean})`;
  });

const page = `---
title: Specification
description: The trackparse spec, rules R0 to R13. Every port implements it and passes the same fixtures.
tableOfContents:
  maxHeadingLevel: 3
editUrl: https://github.com/4matic/trackparse/edit/main/spec/SPEC.md
---

:::note[Spec ${version}]
This page is [\`spec/SPEC.md\`](${blob}SPEC.md), copied in when the site is built. Rule IDs like R6.10
are stable: fixtures cite them, and so do the comments in the code.
:::

${body}`;

const out = join(here, "../src/content/docs/reference/spec.md");
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, page);
console.log(`spec ${version} -> ${out}`);
