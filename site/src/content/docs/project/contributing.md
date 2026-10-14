---
title: Contributing
description: How to report a bug, propose a rule, and get a change merged.
---

## Report a bug

The most useful bug report is one line: the input string, and the output you expected. That's
usually enough to turn it straight into a fixture. If it came from a specific place (a YouTube
title, a Beatport export, a file name), say so, because that decides which mode it belongs to.

Open an issue on [GitHub](https://github.com/4matic/trackparse/issues). You can paste a
[playground](/trackparse/playground/) link: the URL contains the input.

## Change a rule

Behaviour starts in the spec, not in the code:

1. Add or amend a rule in [`spec/SPEC.md`](https://github.com/4matic/trackparse/blob/main/spec/SPEC.md).
   Rule IDs are never renumbered or reused.
2. Add handwritten fixtures that cite the rule: the positive case, the near-miss that must not
   trigger, and one exact case if the rule touches many fields.
3. If the rule is combinatorial (a new joiner, a new version word), teach the generator the atom
   and regenerate.
4. Change every port, JavaScript and Python, and cite the rule ID in a comment.
5. Check that every fixture passes in both ports and that the cross-port diff reports zero
   differences.

Word lists live only in `spec/data`. They're compiled into each port, so a new keyword is a JSON
change plus codegen, not a code change.

## The repository

```text
spec/        SPEC.md, word lists, JSON Schemas, fixtures, the fixture generator
js/          the npm package (reference implementation)
python/      the PyPI package
site/        this website
scripts/     cross-port diff and other tools
```

[AGENTS.md](https://github.com/4matic/trackparse/blob/main/AGENTS.md) has the full repository map,
every command and the release process. Commits follow Conventional Commits, because the changelog
is generated from them.

## License

[MIT](https://github.com/4matic/trackparse/blob/main/LICENSE) © Maksim Maksimov.
