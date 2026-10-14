// @ts-check
import starlight from "@astrojs/starlight";
import { defineConfig } from "astro/config";

const repo = "https://github.com/4matic/trackparse";

export default defineConfig({
  site: "https://4matic.github.io",
  base: "/trackparse",
  trailingSlash: "always",
  // The Python guide was folded into the other pages (every example has a Python tab). PyPI links here.
  redirects: { "/guides/python/": "/trackparse/start/getting-started/" },
  markdown: { smartypants: false },
  // Starlight is a multi-page site. Prefetching every link as it scrolls into view, plus the
  // cross-document view transitions in custom.css, makes page changes instant without a JS router.
  prefetch: { prefetchAll: true, defaultStrategy: "viewport" },
  integrations: [
    starlight({
      title: "trackparse",
      disable404Route: true,
      description:
        "Turn messy track names into structured data: artists, features, remixes, versions and the noise around them. JavaScript and Python, one spec.",
      logo: { src: "./src/assets/logo.svg", replacesTitle: false },
      favicon: "/favicon.svg",
      social: [
        { icon: "github", label: "GitHub", href: repo },
        { icon: "npm", label: "npm", href: "https://www.npmjs.com/package/trackparse" },
      ],
      editLink: { baseUrl: `${repo}/edit/main/site/` },
      customCss: ["./src/styles/custom.css"],
      components: { SocialIcons: "./src/components/SocialIcons.astro" },
      head: [
        { tag: "meta", attrs: { property: "og:image", content: "https://4matic.github.io/trackparse/og.png" } },
        { tag: "meta", attrs: { property: "og:image:width", content: "1200" } },
        { tag: "meta", attrs: { property: "og:image:height", content: "630" } },
        { tag: "meta", attrs: { name: "twitter:card", content: "summary_large_image" } },
      ],
      expressiveCode: {
        themes: ["github-dark-default", "github-light-default"],
        styleOverrides: { borderRadius: "0.6rem" },
      },
      sidebar: [
        {
          label: "Start here",
          items: [
            { label: "Introduction", slug: "start/introduction" },
            { label: "Getting started", slug: "start/getting-started" },
            { label: "Playground", link: "/playground/", badge: { text: "live", variant: "success" } },
          ],
        },
        {
          label: "Guides",
          items: [
            { label: "What it understands", slug: "guides/what-it-understands" },
            { label: "Artists and credits", slug: "guides/artists" },
            { label: "Versions and remixes", slug: "guides/versions" },
            { label: "Junk, flags and prefixes", slug: "guides/junk-and-flags" },
            { label: "Modes", slug: "guides/modes" },
            { label: "Formatting", slug: "guides/formatting" },
            { label: "Tracklists", slug: "guides/tracklists" },
            { label: "Editing tracklists", slug: "guides/editing-tracklists" },
          ],
        },
        {
          label: "Reference",
          items: [
            { label: "parse and ParsedTrack", slug: "reference/parse" },
            { label: "Options", slug: "reference/options" },
            { label: "Other functions", slug: "reference/functions" },
            { label: "Tracklist API", slug: "reference/tracklist" },
            { label: "Warnings", slug: "reference/warnings" },
            { label: "Specification", slug: "reference/spec" },
          ],
        },
        {
          label: "Project",
          items: [
            { label: "How it works", slug: "project/how-it-works" },
            { label: "How it's tested", slug: "project/testing" },
            { label: "Ports", slug: "project/ports" },
            { label: "Compared to others", slug: "project/comparison" },
            { label: "Limitations and roadmap", slug: "project/roadmap" },
            { label: "Contributing", slug: "project/contributing" },
          ],
        },
      ],
    }),
  ],
});
