//! Pipeline: R1 → R2 → R4 → R3/R6 → R7 → R8 → R9, plus R10 parse_artists.

use std::collections::HashSet;
use std::sync::{Arc, OnceLock};

use crate::artists::parse_artist_side;
use crate::assemble::{dedup_artists, full_title_of, to_version};
use crate::context::{known_keys, warn, Ctx};
use crate::mode::{prelude, Piece, Prelude};
use crate::normalize::normalize;
use crate::options::Resolved;
use crate::prefix::strip_prefixes;
use crate::scanner::{scan, split_at_spaced_dashes, trim_skel, Skel};
use crate::separator::{find_separator, SeparatorInput};
use crate::tables::{tables_for, Tables};
use crate::title::{add_peeled, parse_title_side, Extracted};
use crate::types::{Artist, ArtistSource, Flags, Mode, RawJunk, RawTrack, Warning};
use crate::words::{chars, js_trim_end};

/// Offset used for uploader text so it sorts before everything from the input.
const UPLOADER_BASE: i64 = -1_000_000_000;

fn skel_of(piece: &Piece, ctx: &mut Ctx<'_>) -> Skel {
    let (skel, unbalanced) = scan(&piece.text, piece.base);
    if unbalanced {
        warn(ctx, Warning::UnbalancedBrackets);
    }
    trim_skel(&skel)
}

/// R6.8: uploader minus a trailing ` - Topic` and `VEVO`.
fn uploader_skel(uploader: Option<&str>) -> Option<Skel> {
    let mut u = normalize(uploader?);
    if let Some(rest) = u.strip_suffix(" - Topic") {
        u = rest.to_string();
    }
    if let Some(rest) = u.strip_suffix("VEVO") {
        u = js_trim_end(rest).to_string();
    }
    if u.is_empty() {
        return None;
    }
    Some(scan(&chars(&u), UPLOADER_BASE).0)
}

/// Resolved options with the tables built once (JS `createParser` / `createListParser`).
pub(crate) struct Engine {
    options: Resolved,
    tables: Arc<Tables>,
    known_keys: HashSet<String>,
    known_max_len: usize,
}

/// The R1, R2 and R4 stage of `run`, kept so the tracklist probe's work is not redone.
struct Staged<'a> {
    engine: &'a Engine,
    ctx: Ctx<'a>,
    pre: Prelude,
    track: RawTrack,
    /// The skeleton after R4; `None` when the input is empty after R1 (R9.5: `track` is final).
    working: Option<Skel>,
}

fn by_pos<T>(mut items: Vec<T>, pos: impl Fn(&T) -> i64) -> Vec<T> {
    items.sort_by_key(|item| pos(item)); // stable, like the JS sort
    items
}

impl Engine {
    pub(crate) fn new(options: Resolved) -> Engine {
        let tables = tables_for(options.keywords.as_ref());
        let (known_keys, known_max_len) = known_keys(&options.known_artists);
        Engine { options, tables, known_keys, known_max_len }
    }

    /// The engine behind `parse` / `parse_artists` without options.
    pub(crate) fn shared() -> &'static Engine {
        static DEFAULT: OnceLock<Engine> = OnceLock::new();
        DEFAULT.get_or_init(|| Engine::new(Resolved::default()))
    }

    fn ctx(&self) -> Ctx<'_> {
        Ctx {
            t: &self.tables,
            known_keys: &self.known_keys,
            known_max_len: self.known_max_len,
            split_and: self.options.split_and,
            warnings: Vec::new(),
            list_seq: 0,
        }
    }

    fn stage(&self, input: &str) -> Staged<'_> {
        let mut ctx = self.ctx();
        let norm = normalize(input);
        let pre = prelude(&norm, self.options.mode, &mut ctx);
        let mut track = RawTrack::empty(input, pre.mode);
        if norm.is_empty() {
            return Staged { engine: self, ctx, pre, track, working: None }; // R9.5
        }

        // R4
        let prefix = strip_prefixes(&pre.working.text, pre.working.base, pre.mode, &ctx);
        track.timestamp = prefix.timestamp;
        track.position = prefix.position;
        let working = skel_of(&Piece { text: prefix.rest, base: prefix.base }, &mut ctx);
        Staged { engine: self, ctx, pre, track, working: Some(working) }
    }

    pub(crate) fn run(&self, input: &str, list_context: bool) -> RawTrack {
        finish(self.stage(input), list_context)
    }

    /// R10
    pub(crate) fn run_artists(&self, input: &str) -> Vec<Artist> {
        let mut ctx = self.ctx();
        let norm = normalize(input);
        if norm.is_empty() {
            return Vec::new();
        }
        let side = parse_artist_side(&scan(&chars(&norm), 0).0, &mut ctx);
        let mut artists = dedup_artists(side.credits);
        for a in &mut artists {
            a.source = ArtistSource::Artist;
        }
        artists
    }

    /// R1, R2 and R4 only, as `run` does them: no artist/title parsing. `finish` completes the
    /// parse from the same stage, so a probed line is not normalized twice.
    #[allow(dead_code)] // the tracklist module (R13) is the caller
    pub(crate) fn probe(&self, input: &str) -> LineProbe<'_> {
        let staged = self.stage(input);
        LineProbe {
            position: staged.track.position.is_some(),
            spaced_dash: staged.pre.pipe_title.is_none()
                && staged
                    .working
                    .as_ref()
                    .is_some_and(|w| split_at_spaced_dashes(w, true).len() >= 2),
            empty: staged.working.is_none(),
            staged,
        }
    }
}

/// R6–R9 on a staged input.
fn finish(st: Staged<'_>, list_context: bool) -> RawTrack {
    let Staged { engine, mut ctx, pre, mut track, working } = st;
    let Some(working) = working else { return track };

    let mut out = Extracted::default();
    let artist_skel: Option<Skel>;
    let title_skel: Skel;
    let relaxed: bool;
    if let Some(pipe_title) = &pre.pipe_title {
        // R2.4 step 3: `artist | title`, R6 skipped.
        title_skel = skel_of(pipe_title, &mut ctx);
        artist_skel = (!working.is_empty()).then_some(working);
        relaxed = artist_skel.is_some();
    } else {
        let sep = find_separator(
            SeparatorInput {
                skel: &working,
                mode: pre.mode,
                uploader: uploader_skel(engine.options.uploader.as_deref()),
                list_context,
            },
            &mut ctx,
        );
        add_peeled(&mut out, sep.peeled);
        artist_skel = sep.artist;
        title_skel = sep.title;
        relaxed = sep.split;
    }

    let artist_side =
        artist_skel.as_ref().map(|s| parse_artist_side(s, &mut ctx)).unwrap_or_default();
    let title_side =
        parse_title_side(&title_skel, relaxed, pre.mode == Mode::Youtube, &mut out, &mut ctx);

    // R9
    let versions = by_pos(out.versions, |v| v.pos);
    if versions.iter().any(|v| v.ambiguous_mix) {
        warn(&mut ctx, Warning::AmbiguousMixCredit);
    }
    let mut credits = artist_side.credits;
    credits.extend(by_pos(out.credits, |c| c.pos));
    track.artists = dedup_artists(credits);
    track.title = title_side.title;
    track.versions = versions.into_iter().map(to_version).collect();
    track.full_title = full_title_of(&track.title, &track.versions);
    // R9.6: the first year in text order.
    let mut years = artist_side.years;
    years.extend(out.years);
    track.year = by_pos(years, |y| y.pos).first().map(|y| y.year);
    track.flags = Flags {
        explicit: out.explicit || artist_side.explicit,
        clean: out.clean || artist_side.clean,
        unknown_artist: artist_side.unknown_artist || out.unknown_artist,
        unknown_title: title_side.unknown_title,
    };
    let mut junk = pre.junk;
    junk.extend(artist_side.junk);
    junk.extend(out.junk);
    track.junk =
        by_pos(junk, |j| j.pos).into_iter().map(|j| RawJunk { raw: j.raw, kind: j.kind }).collect();
    track.warnings = ctx.warnings;
    track
}

/// Facts about a line's prefix and separator search, for the tracklist module (R13.4.2, R13.5).
#[allow(dead_code)] // the tracklist module (R13) is the caller
pub(crate) struct LineProbe<'a> {
    /// R4.2 took a position.
    pub position: bool,
    /// The skeleton R6 searches (after R2 and R4) has a spaced dash (R6.1).
    pub spaced_dash: bool,
    /// The input is empty after R1.
    pub empty: bool,
    staged: Staged<'a>,
}

#[allow(dead_code)] // the tracklist module (R13) is the caller
impl LineProbe<'_> {
    /// The parse of the probed input (R6–R9 only).
    pub(crate) fn finish(self, list_context: bool) -> RawTrack {
        finish(self.staged, list_context)
    }
}

/// Internal: parsing hooks for the tracklist module, with the options compiled once.
#[allow(dead_code)] // the tracklist module (R13) is the caller
pub(crate) struct ListParser {
    engine: Engine,
}

#[allow(dead_code)] // the tracklist module (R13) is the caller
impl ListParser {
    pub(crate) fn new(options: Resolved) -> ListParser {
        ListParser { engine: Engine::new(options) }
    }

    /// `parse` with the R13.4.2 list-context flag (it only matters in tracklist mode, R6.10).
    pub(crate) fn parse(&self, input: &str, list_context: bool) -> RawTrack {
        self.engine.run(input, list_context)
    }

    pub(crate) fn probe(&self, input: &str) -> LineProbe<'_> {
        self.engine.probe(input)
    }
}
