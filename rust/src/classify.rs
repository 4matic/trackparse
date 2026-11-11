//! R5: classification of a group's inner text (also used for dash suffixes and pipe segments).

use std::collections::HashSet;

use crate::context::Ctx;
use crate::credits::{split_credits, Credit, ListOptions, ListResult};
use crate::scanner::scan;
use crate::tables::{is_unknown_token, RunInfo};
use crate::types::{ArtistRole, ArtistSource, VersionDelimiter};
use crate::words::{
    ascii_lower, at, before, chars, dedup_key, in_vocab, is_all_digits, js_trim, span_texts,
    split_words, string, sub, tail, utf16_off, word_key, word_spans, year_of, WordSpan,
};

#[derive(Debug, Clone)]
pub(crate) struct VersionDraft {
    /// A version type name; `keywords.versionHeads` values pass through as given.
    pub vtype: String,
    pub raw: String,
    pub artists: Vec<Credit>,
    pub modifiers: Vec<String>,
    pub descriptor: Option<String>,
    pub year: Option<u32>,
    pub unknown_artist: bool,
    pub delimiter: VersionDelimiter,
    pub pos: i64,
    /// R5.7.2 ambiguousMixCredit; emitted only when the version is used.
    pub ambiguous_mix: bool,
    /// Recognised by the R5.7.3 prefix form (R6.2 condition (ii) does not apply).
    pub prefix_form: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Flag {
    Explicit,
    Clean,
}

#[derive(Debug, Clone)]
pub(crate) enum Classification {
    Junk(String),
    Feat(ListResult),
    Producer(ListResult),
    Flag(Flag),
    Year(u32),
    Versions(Vec<VersionDraft>),
    Unknown,
}

/// Where the classified text sits: credits' source, versions' delimiter, absolute offset.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Site {
    pub source: ArtistSource,
    pub delimiter: VersionDelimiter,
    pub pos: i64,
}

const EXPLICIT_FLAGS: [&str; 4] = ["explicit", "explicit version", "dirty", "dirty version"];
const CLEAN_FLAGS: [&str; 3] = ["clean", "clean version", "radio clean"];

/// R5: classify `inner` (already trimmed and whitespace-collapsed).
pub(crate) fn classify(inner: &str, site: Site, ctx: &mut Ctx<'_>) -> Classification {
    let words = split_words(inner);
    let Some(first) = words.first() else { return Classification::Unknown };
    if let Some(kind) = junk_kind_of(&words, ctx) {
        return Classification::Junk(kind);
    }
    if let Some(credit) = feat_or_producer(inner, site, ctx) {
        return credit;
    }
    if let Some(flag) = flag_of(&words) {
        return Classification::Flag(flag);
    }
    if words.len() == 1 && is_all_digits(first) {
        if let Some(year) = year_of(first) {
            return Classification::Year(year);
        }
    }
    match multi_version(inner, site, ctx).or_else(|| single_version(inner, site, ctx)) {
        Some(versions) => Classification::Versions(versions),
        None => Classification::Unknown,
    }
}

// ---------------------------------------------------------------------------
// R5.1 Junk

/// R5.1: the junk kind if the whole word list is junk vocabulary, else `None`.
pub(crate) fn junk_kind_of(words: &[String], ctx: &Ctx<'_>) -> Option<String> {
    let t = ctx.t;
    let mut first_kind: Option<&String> = None;
    let mut i = 0;
    while let Some(w) = words.get(i) {
        if let Some(m) = t.junk_or_genre.match_at(words, i) {
            first_kind = first_kind.or(Some(m.value));
            i += m.length;
            continue;
        }
        // R5.1: connectors and year words may sit between junk phrases: `Official Video 2015`.
        if t.junk_connectors.contains(w.as_str())
            || t.junk_connectors.contains(word_key(w).as_str())
            || year_of(w).is_some()
        {
            i += 1;
            continue;
        }
        first_kind = None;
        break;
    }
    // JS truthiness: an empty kind (possible through `keywords.junk`) does not count.
    if let Some(kind) = first_kind.filter(|k| !k.is_empty()) {
        return Some(kind.clone());
    }
    let last = words.last()?;
    (words.len() >= 2 && in_vocab(last, &t.label_suffixes)).then(|| "label".to_string())
}

// ---------------------------------------------------------------------------
// R5.2 Feat, R5.3 Producer

fn feat_or_producer(inner: &str, site: Site, ctx: &mut Ctx<'_>) -> Option<Classification> {
    let t = ctx.t;
    let text = chars(inner);
    let spans = word_spans(&text);
    let words = span_texts(&spans);
    let feat = t.feat_markers.match_at(&words, 0).map(|m| m.length);
    let bracket_only = if feat.is_some() {
        None
    } else {
        t.bracket_only_feat.match_at(&words, 0).map(|m| m.length)
    };
    let feat_len = feat.or(bracket_only).unwrap_or(0);
    if feat_len > 0 && words.len() > feat_len {
        let stop = bracket_only.is_some()
            && words.get(feat_len).is_some_and(|next| in_vocab(next, &t.stopwords));
        if !stop {
            let r = credits_after(&text, &spans, feat_len, ArtistRole::Featured, site, ctx);
            return r.map(Classification::Feat);
        }
    }
    let prod = t.producer_markers.match_at(&words, 0)?;
    let mut len = prod.length;
    if words.get(len).is_some_and(|w| word_key(w) == "by") {
        len += 1;
    }
    if words.len() > len {
        return credits_after(&text, &spans, len, ArtistRole::Producer, site, ctx)
            .map(Classification::Producer);
    }
    None
}

fn credits_after(
    inner: &[char],
    spans: &[WordSpan],
    marker_words: usize,
    role: ArtistRole,
    site: Site,
    ctx: &mut Ctx<'_>,
) -> Option<ListResult> {
    let first = spans.first()?;
    let last_marker = spans.get(marker_words.checked_sub(1)?)?;
    let first_name = spans.get(marker_words)?;
    let lead = string(sub(inner, first.start, last_marker.end));
    // Offsets are UTF-16 units (see scanner.rs).
    let base = site.pos + 1 + utf16_off(sub(inner, 0, first_name.start));
    let opts = ListOptions {
        role,
        source: site.source,
        lead: Some(lead),
        split_with: false,
        feat_markers_join: role == ArtistRole::Featured,
    };
    Some(list_from_text(tail(inner, first_name.start), base, ctx, &opts))
}

/// Split a plain string (nested brackets protected by a fresh scan) as a credit list.
pub(crate) fn list_from_text(
    text: &[char],
    base: i64,
    ctx: &mut Ctx<'_>,
    opts: &ListOptions,
) -> ListResult {
    split_credits(&scan(text, base).0, opts, ctx)
}

// ---------------------------------------------------------------------------
// R5.4 Flag

fn flag_of(words: &[String]) -> Option<Flag> {
    let key = words.iter().map(|w| word_key(w)).collect::<Vec<_>>().join(" ");
    if EXPLICIT_FLAGS.contains(&key.as_str()) {
        Some(Flag::Explicit)
    } else if CLEAN_FLAGS.contains(&key.as_str()) {
        Some(Flag::Clean)
    } else {
        None
    }
}

// ---------------------------------------------------------------------------
// R5.6 Multi-version

/// Split at top-level ` / ` or `; ` (outside nested brackets).
fn split_multi_parts(inner: &[char]) -> Vec<String> {
    let mut parts: Vec<&[char]> = Vec::new();
    let mut depth = 0usize;
    let mut start = 0usize;
    for (i, &ch) in inner.iter().enumerate() {
        if matches!(ch, '(' | '[' | '{') {
            depth += 1;
        } else if matches!(ch, ')' | ']' | '}') && depth > 0 {
            depth -= 1;
        } else if depth == 0
            && ch == '/'
            && before(inner, i) == Some(' ')
            && at(inner, i + 1) == Some(' ')
        {
            parts.push(sub(inner, start, i - 1));
            start = i + 2;
        } else if depth == 0 && ch == ';' && at(inner, i + 1) == Some(' ') {
            parts.push(sub(inner, start, i));
            start = i + 2;
        }
    }
    parts.push(tail(inner, start));
    parts.into_iter().map(|p| js_trim(&string(p)).to_string()).collect()
}

fn multi_version(inner: &str, site: Site, ctx: &mut Ctx<'_>) -> Option<Vec<VersionDraft>> {
    let parts = split_multi_parts(&chars(inner));
    if parts.len() < 2 {
        return None;
    }
    let mut out = Vec::new();
    for part in &parts {
        if part.is_empty() {
            return None;
        }
        out.extend(single_version(part, site, ctx)?);
    }
    Some(out)
}

// ---------------------------------------------------------------------------
// R5.7 Version

fn single_version(raw: &str, site: Site, ctx: &mut Ctx<'_>) -> Option<Vec<VersionDraft>> {
    let text = chars(raw);
    let spans = word_spans(&text);
    let words = span_texts(&spans);
    if let Some(v) = head_by(raw, &text, &spans, &words, site, ctx) {
        return Some(vec![v]);
    }
    if let Some(v) = head_scan(raw, &words, site, ctx) {
        return Some(vec![v]);
    }
    prefix_form(raw, &words, site, ctx).map(|v| vec![v])
}

fn base_version(raw: &str, vtype: &str, site: Site) -> VersionDraft {
    VersionDraft {
        vtype: vtype.to_string(),
        raw: raw.to_string(),
        artists: Vec::new(),
        modifiers: Vec::new(),
        descriptor: None,
        year: None,
        unknown_artist: false,
        delimiter: site.delimiter,
        pos: site.pos,
        ambiguous_mix: false,
        prefix_form: false,
    }
}

fn remixer_opts() -> ListOptions {
    ListOptions {
        role: ArtistRole::Remixer,
        source: ArtistSource::Version,
        lead: None,
        split_with: false,
        feat_markers_join: false,
    }
}

/// R9.2: remixers are deduped within their own version only.
fn dedup_remixers(credits: Vec<Credit>) -> Vec<Credit> {
    let mut seen: HashSet<String> = HashSet::new();
    credits.into_iter().filter(|c| seen.insert(dedup_key(&c.name))).collect()
}

fn set_credits(v: &mut VersionDraft, text: &[char], site: Site, ctx: &mut Ctx<'_>) {
    let r = list_from_text(text, site.pos, ctx, &remixer_opts());
    v.artists = dedup_remixers(r.credits);
    if r.unknown {
        v.unknown_artist = true;
    }
    // R7.5: a dropped year-only name sets the version's year if it is still null.
    if let Some(dropped) = r.years.first() {
        v.year = v.year.or(Some(dropped.year));
    }
}

/// R5.7.1: `Remix by Skrillex`.
fn head_by(
    raw: &str,
    text: &[char],
    spans: &[WordSpan],
    words: &[String],
    site: Site,
    ctx: &mut Ctx<'_>,
) -> Option<VersionDraft> {
    let t = ctx.t;
    let head = t.heads.match_at(words, 0)?;
    let by = words.get(head.length)?;
    let rest = spans.get(head.length + 1)?;
    if word_key(by) != "by" {
        return None;
    }
    let mut v = base_version(raw, head.value, site);
    set_credits(&mut v, tail(text, rest.start), site, ctx);
    Some(v)
}

struct RunItem {
    start: usize,
    end: usize,
    info: RunInfo,
    year: Option<u32>,
}

/// Walk left from `end` collecting the modifier run (R5.7.2).
fn collect_run(words: &[String], end: usize, ctx: &Ctx<'_>) -> Vec<RunItem> {
    let mut items: Vec<RunItem> = Vec::new();
    let mut j = end;
    while j > 0 {
        if let Some(m) = ctx.t.run_words.match_ending(words, j) {
            items.push(RunItem { start: j - m.length, end: j, info: m.value.clone(), year: None });
            j -= m.length;
            continue;
        }
        let Some(year) = words.get(j - 1).and_then(|w| year_of(w)) else { break };
        items.push(RunItem { start: j - 1, end: j, info: RunInfo::default(), year: Some(year) });
        j -= 1;
    }
    items.reverse();
    items
}

/// R5.7.2: `Extended VIP Mix`, `Skrillex Remix`, `Taylor's Version`.
fn head_scan(raw: &str, words: &[String], site: Site, ctx: &mut Ctx<'_>) -> Option<VersionDraft> {
    let t = ctx.t;
    let head = t.heads.match_ending(words, words.len())?;
    let head_type: &str = head.value;
    let head_start = words.len() - head.length;
    let run = collect_run(words, head_start, ctx);
    let credit_end = run.first().map_or(head_start, |r| r.start);
    let credit_words = words.get(..credit_end).unwrap_or(&[]);
    let left = run.len().checked_sub(1);

    // R5.7.2 type resolution. Generic = canonical type mix/edit/version, any spelling.
    let generic = t.generic_head_types.contains(head_type);
    let mut vtype: String = head_type.to_string();
    let mut type_item: Option<usize> = None;
    let mut via_mix = false;
    let nearest_head = if generic { nearest_non_generic_head(&run, ctx) } else { None };
    // JS truthiness: an empty type name does not count.
    let truthy = |s: Option<&String>| s.filter(|s| !s.is_empty()).cloned();
    let left_capable =
        left.and_then(|i| run.get(i)).and_then(|r| truthy(r.info.type_capable.as_ref()));
    let nearest_type =
        nearest_head.and_then(|i| run.get(i)).and_then(|r| truthy(r.info.head.as_ref()));
    if let (true, Some(capable)) = (generic, left_capable) {
        vtype = capable;
        type_item = left;
    } else if let Some(head) = nearest_type {
        // `2011 Remastered Version` → remaster
        vtype = head;
        type_item = nearest_head;
    } else if head_type == "mix" && !credit_words.is_empty() {
        vtype = "remix".to_string();
        via_mix = true;
    }
    let mut v = base_version(raw, &vtype, site);

    let mut descriptors: Vec<String> = Vec::new();
    for (i, item) in run.iter().enumerate() {
        if let Some(year) = item.year {
            v.year = v.year.or(Some(year));
            continue;
        }
        let modifier = item.info.type_capable.as_ref().or(item.info.descriptor.as_ref());
        if let Some(modifier) = modifier {
            if Some(i) != type_item && !v.modifiers.contains(modifier) {
                v.modifiers.push(modifier.clone());
            }
        } else if item.info.genre {
            descriptors.push(words.get(item.start..item.end).unwrap_or(&[]).join(" "));
        }
    }

    if let Some(last_credit_word) = credit_words.last() {
        let credit = credit_words.join(" ");
        let last_word = ascii_lower(last_credit_word);
        if is_unknown_token(&credit, t) {
            v.unknown_artist = true;
        } else if vtype == "version" || vtype == "cover" || is_all_junk_phrases(credit_words, ctx) {
            // R5.7.2: `Japanese Version`, `Taylor's Version`, `Adele Cover`, `Video Edit`.
            descriptors.insert(0, credit);
        } else {
            let mut name = chars(&credit);
            if last_word.ends_with("'s") {
                name.truncate(name.len().saturating_sub(2));
            }
            set_credits(&mut v, &name, site, ctx);
            if via_mix && v.artists.last().is_some_and(|a| split_words(&a.name).len() >= 3) {
                v.ambiguous_mix = true;
            }
        }
    }
    v.descriptor = if descriptors.is_empty() { None } else { Some(descriptors.join(" ")) };
    Some(v)
}

/// R5.7.2 type rule 2: the non-generic head in the run nearest the (generic) head.
fn nearest_non_generic_head(run: &[RunItem], ctx: &Ctx<'_>) -> Option<usize> {
    (0..run.len()).rev().find(|&i| {
        run.get(i)
            .and_then(|r| r.info.head.as_ref())
            .is_some_and(|head| !ctx.t.generic_head_types.contains(head.as_str()))
    })
}

/// R5.7.2: the credit span is made entirely of junk phrases (`Video` in `Video Edit`).
fn is_all_junk_phrases(words: &[String], ctx: &Ctx<'_>) -> bool {
    let mut i = 0;
    while i < words.len() {
        match ctx.t.junk_phrases.match_at(words, i) {
            Some(m) => i += m.length,
            None => return false,
        }
    }
    !words.is_empty()
}

/// R5.7.3: `Live at Wembley 1986`, `Remastered 2015`, `Sped Up`.
fn prefix_form(raw: &str, words: &[String], site: Site, ctx: &Ctx<'_>) -> Option<VersionDraft> {
    let m = ctx.t.prefix_forms.match_at(words, 0)?;
    let mut v = base_version(raw, m.value, site);
    v.prefix_form = true;
    let mut rest: Vec<&str> = Vec::new();
    for w in words.get(m.length..).unwrap_or(&[]) {
        match if v.year.is_none() { year_of(w) } else { None } {
            Some(year) => v.year = Some(year),
            None => rest.push(w),
        }
    }
    v.descriptor = if rest.is_empty() { None } else { Some(rest.join(" ")) };
    Some(v)
}
