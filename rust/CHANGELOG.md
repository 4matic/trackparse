# Changelog

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
