// One language choice for the whole site. The docs' code tabs (`<Tabs syncKey="lang">`) keep
// theirs in localStorage under Starlight's key, as the tab label. The install switch on the
// landing page and the playground's code panels read and write the same entry, so picking Rust
// or Go in one place picks it everywhere.

export type Lang = "js" | "py" | "rs" | "go";

/** Starlight's key for `syncKey="lang"` (see its Tabs.astro). */
const KEY = "starlight-synced-tabs__lang";

/** The tab labels used on every docs page. */
export const LANG_LABEL: Record<Lang, string> = { js: "JavaScript", py: "Python", rs: "Rust", go: "Go" };

/** Where each package lives, and where the docs explain how to install it. */
export const PACKAGE: Record<Lang, { install: string; registry: string; url: string }> = {
  js: { install: "npm install trackparse", registry: "npm", url: "https://www.npmjs.com/package/trackparse" },
  py: { install: "pip install trackparse", registry: "PyPI", url: "https://pypi.org/project/trackparse/" },
  rs: { install: "cargo add trackparse", registry: "crates.io", url: "https://crates.io/crates/trackparse" },
  go: { install: "go get github.com/4matic/trackparse/go@latest", registry: "pkg.go.dev", url: "https://pkg.go.dev/github.com/4matic/trackparse/go" },
};

export function getLang(): Lang {
  try {
    const label = localStorage.getItem(KEY);
    for (const l of Object.keys(LANG_LABEL) as Lang[]) if (LANG_LABEL[l] === label) return l;
  } catch {}
  return "js";
}

/** Remember the choice and switch any synced docs tabs on this page, without moving focus. */
export function setLang(lang: Lang) {
  const label = LANG_LABEL[lang];
  try {
    localStorage.setItem(KEY, label);
  } catch {}
  type TabsEl = HTMLElement & { switchTab?: (tab: Element, index: number, sync?: boolean) => void };
  for (const el of document.querySelectorAll<TabsEl>('starlight-tabs[data-sync-key="lang"]')) {
    const tabs = [...el.querySelectorAll(':scope > .tablist-wrapper [role="tab"]')];
    const i = tabs.findIndex((t) => t.textContent?.trim() === label);
    if (i >= 0) el.switchTab?.(tabs[i], i, false);
  }
}

/** Call `fn` when the reader picks a language in a synced docs tab on this page. */
export function onDocsLang(fn: (lang: Lang) => void) {
  const handler = (e: Event) => {
    const t = e.target instanceof Element ? e.target.closest('starlight-tabs[data-sync-key="lang"] [role="tab"]') : null;
    const tabs = t?.closest("starlight-tabs");
    if (!tabs) return;
    // Starlight selects the tab in its own handler; read the selected label once that has run.
    // The label comes from the page, not from localStorage, which may be blocked.
    setTimeout(() => {
      const label = tabs.querySelector(':scope > .tablist-wrapper [role="tab"][aria-selected="true"]')?.textContent?.trim();
      const lang = (Object.keys(LANG_LABEL) as Lang[]).find((l) => LANG_LABEL[l] === label);
      fn(lang ?? getLang());
    });
  };
  document.addEventListener("click", handler);
  document.addEventListener("keydown", handler);
}
