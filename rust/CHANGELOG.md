# Changelog

## [0.6.1](https://github.com/4matic/trackparse/compare/rust-v0.6.0...rust-v0.6.1) (2026-11-14)


### Documentation

* **rust:** link every item to docs.rs and to the site reference and spec rules, with playground links and the other packages' READMEs, changelogs and API docs ([dbb118f](https://github.com/4matic/trackparse/commit/dbb118fec5e5c80419568eab7b1d0a2dbb96d39e))

## [0.6.0](https://github.com/4matic/trackparse/compare/rust-v0.5.1...rust-v0.6.0) (2026-11-14)


### Documentation

* **rust:** list the Go module among the other languages, and its Unicode tables ([2e9056c](https://github.com/4matic/trackparse/commit/2e9056c0605b5e26b9899e288e7388786502b590))
* **rust:** the crate description names the Go module ([7f44477](https://github.com/4matic/trackparse/commit/7f44477afcbfa4b8843324a3d61f9c68c96497bc))
* **rust:** the README shows feat_marker, links the spec changelog, and drops the stale 0.5 version ([9668ee6](https://github.com/4matic/trackparse/commit/9668ee61761d69010b61cddd6e8fd8dd9971798e))
* the Go module is described as published, with exact Unicode versions and its own Types cell ([4038ab9](https://github.com/4matic/trackparse/commit/4038ab9961d7c11abb57793fccd6b9e1f5d0d063))

## [0.5.1](https://github.com/4matic/trackparse/compare/rust-v0.5.0...rust-v0.5.1) (2026-11-11)


### Documentation

* **rust:** README with warnings, errors, limitations and links to the JavaScript and Python packages ([c13ec1f](https://github.com/4matic/trackparse/commit/c13ec1f6fe7fbf235fda0ca142c01b7a5caa3021))

## [0.5.0](https://github.com/4matic/trackparse/compare/rust-v0.4.0...rust-v0.5.0) (2026-11-11)


### Features

* initial trackparse spec, fixtures and js package ([cf346e8](https://github.com/4matic/trackparse/commit/cf346e846b2fd1b8675f53225f16462057ad8c6e))
* **rust:** serde feature, and the public API ready for publishing ([842dabe](https://github.com/4matic/trackparse/commit/842dabedffd578258629c1f5656c2614280e34f9))
* **rust:** single-track parser, R0–R12 (parse, parse_artists, format_track, normalize, Parser) ([e15fdc6](https://github.com/4matic/trackparse/commit/e15fdc64f9954b5848f0961a22b1f521739d6f43))
* **rust:** tracklists, R13 (parse_tracklist, apply_edits) ([abc0dd4](https://github.com/4matic/trackparse/commit/abc0dd419bb1e446f4cf02961047982c91555642))


### Bug Fixes

* **rust:** keep the R9.2 dedup near-linear on thousands of duplicate credits ([3f5e538](https://github.com/4matic/trackparse/commit/3f5e5386ff1c840f8e9661465aa59608529e2e69))
* **rust:** lowercase for the dedup key from generated tables, not the compiler's Unicode version ([0e8232a](https://github.com/4matic/trackparse/commit/0e8232a160a24bcf0c4301ff81efbcac5c8a0216))
* **rust:** settle the public API before the first publish ([06fc193](https://github.com/4matic/trackparse/commit/06fc193e5b647fe0d165cb294fac2db0392ee77b))
* **site:** keep inline code in one box instead of splitting it across lines ([200937f](https://github.com/4matic/trackparse/commit/200937fec6b152410102c420c001a08ef9d4e835))


### Documentation

* keep the site and READMEs in sync with the code, link README sections to the docs ([20b9d3d](https://github.com/4matic/trackparse/commit/20b9d3db5a22705bfe91e805564b295fc67c0641))
* **rust:** README for crates.io, replacing the planned stub ([5380473](https://github.com/4matic/trackparse/commit/5380473ba32b464edf69b074a513a3c544624673))
* **rust:** Unicode version, performance and serde edge cases in the README ([ce0a477](https://github.com/4matic/trackparse/commit/ce0a47738c89c1059d5031c378d73117770ae760))
