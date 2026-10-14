// Lucide icons as SVG strings, for HTML the playground builds in the browser.
// (Static markup uses @lucide/astro components instead.)
import { ArrowRight, Info, TriangleAlert, Wand, type IconNode } from "lucide";

function svg(node: IconNode, size = 16): string {
  const children = node
    .map(([tag, attrs]) => `<${tag} ${Object.entries(attrs).map(([k, v]) => `${k}="${v}"`).join(" ")}/>`)
    .join("");
  return `<svg class="lucide" xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${children}</svg>`;
}

export const icon = {
  warning: svg(TriangleAlert),
  info: svg(Info),
  fix: svg(Wand, 14),
  arrow: svg(ArrowRight, 14),
};
