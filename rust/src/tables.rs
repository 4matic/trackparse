//! Vocabulary tables compiled from spec/data, optionally extended by `options.keywords` (R12).

use std::collections::{HashMap, HashSet};
use std::sync::{Arc, OnceLock};

use crate::data::{
    DESCRIPTORS_DATA, EXTENSIONS_DATA, FEAT_MARKERS_DATA, GENRES_DATA, JOINERS_DATA, JUNK_DATA,
    NO_SPLIT_BEFORE_DATA, PLATFORM_SUFFIXES_DATA, STOPWORDS_DATA, UNKNOWN_TOKENS_DATA,
    VERSION_KEYWORDS_DATA,
};
use crate::options::{js_entries, Keywords};
use crate::value::V;
use crate::words::{ascii_lower, js_trim, PhraseTable};

#[derive(Debug, Clone)]
pub(crate) struct SpacedJoiner {
    pub raw: &'static str,
    pub canonical: &'static str,
    pub is_and: bool,
    pub case_sensitive: bool,
}

/// Category of a word in the modifier run left of a version head (R5.7.2). Version types are
/// strings: `keywords.versionHeads` values pass through to the output as given.
#[derive(Debug, Clone, Default)]
pub(crate) struct RunInfo {
    pub type_capable: Option<String>,
    pub descriptor: Option<String>,
    pub genre: bool,
    pub head: Option<String>,
}

#[derive(Debug)]
pub(crate) struct Tables {
    pub feat_markers: PhraseTable<()>,
    pub bracket_only_feat: PhraseTable<()>,
    pub producer_markers: PhraseTable<()>,
    /// R8.3: the only unbracketed feat markers on the title side (+ keywords.featMarkers).
    pub title_feat_markers: PhraseTable<()>,
    /// R8.3: the only unbracketed producer markers on the title side.
    pub title_producer_markers: PhraseTable<()>,
    /// Junk phrases plus genres (kind `genre`): the R5.1 tokenizer vocabulary.
    pub junk_or_genre: PhraseTable<String>,
    /// Junk phrases only (R8.4).
    pub junk_phrases: PhraseTable<String>,
    pub junk_connectors: HashSet<&'static str>,
    pub label_suffixes: HashSet<&'static str>,
    pub heads: PhraseTable<String>,
    pub generic_head_types: HashSet<&'static str>,
    /// Everything allowed in the R5.7.2 modifier run, keyed by phrase.
    pub run_words: PhraseTable<RunInfo>,
    pub prefix_forms: PhraseTable<String>,
    pub spaced_joiners: Vec<SpacedJoiner>,
    pub tight_joiners: HashMap<&'static str, &'static str>,
    pub feat_canonical: &'static str,
    pub no_split_before: HashSet<&'static str>,
    pub stopwords: HashSet<&'static str>,
    pub unknown_case_sensitive: HashSet<&'static str>,
    pub unknown_case_insensitive: HashSet<&'static str>,
    pub extensions: HashSet<&'static str>,
    pub platform_suffixes: Vec<&'static str>,
}

fn lower_keys(list: &[String]) -> Vec<String> {
    list.iter().map(|s| js_trim(&ascii_lower(s)).to_string()).filter(|s| !s.is_empty()).collect()
}

/// The string-valued entries of a data object, in `Object.entries` order.
fn data_entries(obj: V) -> Vec<(String, String)> {
    let pairs: Vec<(String, String)> = obj
        .entries()
        .iter()
        .filter_map(|(k, v)| v.as_str().map(|s| ((*k).to_string(), s.to_string())))
        .collect();
    js_entries(&pairs)
}

fn marker_table(base: V, extra: &[String]) -> PhraseTable<()> {
    PhraseTable::new(
        base.strings().map(str::to_string).chain(extra.iter().cloned()).map(|m| (m, ())),
    )
}

fn word_set(list: V) -> HashSet<&'static str> {
    list.strings().collect()
}

pub(crate) fn build_tables(keywords: Option<&Keywords>) -> Tables {
    let none = Keywords::default();
    let kw = keywords.unwrap_or(&none);
    let extra_feat = lower_keys(&kw.feat_markers);

    let mut junk_entries = data_entries(JUNK_DATA.get("phrases"));
    junk_entries.extend(kw.junk.iter().cloned());
    let mut genres: Vec<String> = GENRES_DATA.get("genres").strings().map(str::to_string).collect();
    genres.extend(lower_keys(&kw.genres));
    let junk_phrases = PhraseTable::new(junk_entries.iter().cloned());
    // Junk phrases are added first, so they win over an identical genre key.
    let junk_or_genre = PhraseTable::new(
        junk_entries.iter().cloned().chain(genres.iter().map(|g| (g.clone(), "genre".to_string()))),
    );

    let mut head_entries = data_entries(VERSION_KEYWORDS_DATA.get("heads"));
    head_entries.extend(kw.version_heads.iter().cloned());
    let heads = PhraseTable::new(head_entries.iter().cloned());

    // A Map in JS: the first note of a key fixes its place, later notes merge into it.
    let mut run_info: Vec<(String, RunInfo)> = Vec::new();
    let mut run_index: HashMap<String, usize> = HashMap::new();
    let mut note = |key: &str, patch: &dyn Fn(&mut RunInfo)| {
        let k = js_trim(&ascii_lower(key)).to_string();
        let i = *run_index.entry(k.clone()).or_insert_with(|| {
            run_info.push((k, RunInfo::default()));
            run_info.len() - 1
        });
        if let Some(slot) = run_info.get_mut(i) {
            patch(&mut slot.1);
        }
    };
    for (k, t) in data_entries(VERSION_KEYWORDS_DATA.get("typeCapableModifiers")) {
        note(&k, &|info| info.type_capable = Some(t.clone()));
    }
    let descriptors = DESCRIPTORS_DATA.get("descriptors");
    for d in descriptors.strings().map(str::to_string).chain(lower_keys(&kw.descriptors)) {
        let value = js_trim(&ascii_lower(&d)).to_string();
        note(&d, &|info| info.descriptor = Some(value.clone()));
    }
    for g in &genres {
        note(g, &|info| info.genre = true);
    }
    for (k, t) in &head_entries {
        note(k, &|info| info.head = Some(t.clone()));
    }

    let spaced_joiners = JOINERS_DATA
        .get("spaced")
        .items()
        .iter()
        .filter_map(|j| {
            Some(SpacedJoiner {
                raw: j.get("raw").as_str()?,
                canonical: j.get("canonical").as_str()?,
                is_and: j.get("kind").as_str() == Some("and"),
                case_sensitive: j.get("caseSensitive") == V::Bool(true),
            })
        })
        .collect();
    let tight_joiners = JOINERS_DATA
        .get("tight")
        .items()
        .iter()
        .filter_map(|j| Some((j.get("raw").as_str()?, j.get("canonical").as_str()?)))
        .collect();
    let mut platform_suffixes: Vec<&'static str> =
        PLATFORM_SUFFIXES_DATA.get("suffixes").strings().collect();
    // JS sorts by UTF-16 length, longest first (stable).
    platform_suffixes.sort_by_key(|s| std::cmp::Reverse(s.encode_utf16().count()));

    Tables {
        feat_markers: marker_table(FEAT_MARKERS_DATA.get("markers"), &extra_feat),
        bracket_only_feat: marker_table(FEAT_MARKERS_DATA.get("bracketOnly"), &[]),
        producer_markers: marker_table(FEAT_MARKERS_DATA.get("producer"), &[]),
        title_feat_markers: marker_table(FEAT_MARKERS_DATA.get("titleMarkers"), &extra_feat),
        title_producer_markers: marker_table(FEAT_MARKERS_DATA.get("titleProducer"), &[]),
        junk_or_genre,
        junk_phrases,
        junk_connectors: word_set(JUNK_DATA.get("connectors")),
        label_suffixes: word_set(JUNK_DATA.get("labelSuffixes")),
        heads,
        generic_head_types: word_set(VERSION_KEYWORDS_DATA.get("genericHeads")),
        run_words: PhraseTable::new(run_info),
        prefix_forms: PhraseTable::new(data_entries(VERSION_KEYWORDS_DATA.get("prefixForms"))),
        spaced_joiners,
        tight_joiners,
        feat_canonical: JOINERS_DATA.get("featCanonical").as_str().unwrap_or("feat."),
        no_split_before: word_set(NO_SPLIT_BEFORE_DATA.get("words")),
        stopwords: word_set(STOPWORDS_DATA.get("words")),
        unknown_case_sensitive: word_set(UNKNOWN_TOKENS_DATA.get("caseSensitive")),
        unknown_case_insensitive: word_set(UNKNOWN_TOKENS_DATA.get("caseInsensitive")),
        extensions: word_set(EXTENSIONS_DATA.get("extensions")),
        platform_suffixes,
    }
}

static DEFAULT_TABLES: OnceLock<Arc<Tables>> = OnceLock::new();

pub(crate) fn default_tables() -> Arc<Tables> {
    Arc::clone(DEFAULT_TABLES.get_or_init(|| Arc::new(build_tables(None))))
}

pub(crate) fn tables_for(keywords: Option<&Keywords>) -> Arc<Tables> {
    match keywords {
        Some(k) => Arc::new(build_tables(Some(k))),
        None => default_tables(),
    }
}

/// R7.5 / R8.6: an unknown placeholder name (`ID`, `???`, `Untitled`…).
pub(crate) fn is_unknown_token(text: &str, t: &Tables) -> bool {
    t.unknown_case_sensitive.contains(text)
        || t.unknown_case_insensitive.contains(ascii_lower(text).as_str())
}
