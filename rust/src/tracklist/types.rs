//! Public tracklist types. They mirror `spec/schema/tracklist.schema.json`; field names are
//! snake_case in Rust and the spec's camelCase in JSON (`to_json()`, and `serde` with that
//! feature).

use crate::json::Json;
use crate::types::{json_methods, list, obj, opt, string_enum, ParsedTrack, ToJson};

string_enum! {
    /// What a line of a tracklist is (R13.3).
    ///
    /// ```
    /// use trackparse::tracklist::{parse_tracklist, EntryKind};
    ///
    /// let text = "Tracklist:\n00:00 Intro\n02:10 Noisia - Stigma\n\nhttps://example.com/mix";
    /// let kinds: Vec<EntryKind> = parse_tracklist(text).entries.iter().map(|e| e.kind).collect();
    /// assert_eq!(
    ///     kinds,
    ///     [EntryKind::Header, EntryKind::Marker, EntryKind::Track, EntryKind::Blank, EntryKind::Link]
    /// );
    /// ```
    #[non_exhaustive]
    EntryKind {
        Track => "track", Marker => "marker", Header => "header", Link => "link", Note => "note",
        Blank => "blank",
    }
}

string_enum! {
    /// The line terminator that followed a line (R13.1.1); `None` when the text ended without
    /// one.
    ///
    /// ```
    /// use trackparse::tracklist::{parse_tracklist, Eol};
    ///
    /// let list = parse_tracklist("A - B\r\nC - D");
    /// assert_eq!(list.entries[0].eol, Eol::CrLf);
    /// assert_eq!(list.entries[1].eol, Eol::None);
    /// assert_eq!(Eol::CrLf.as_str(), "\r\n");
    /// ```
    Eol { Lf => "\n", CrLf => "\r\n", Cr => "\r", None => "" }
}

/// A line cut into four pieces that join back to it: `lead + time + gap + rest == raw`
/// (R13.1.3).
///
/// ```
/// use trackparse::tracklist::parse_tracklist;
///
/// let list = parse_tracklist("- [03:45] Phace - Cold Champagne");
/// let parts = &list.entries[0].parts;
/// assert_eq!(parts.lead, "- [");
/// assert_eq!(parts.time, "03:45");
/// assert_eq!(parts.gap, "] ");
/// assert_eq!(parts.rest, "Phace - Cold Champagne");
/// ```
#[derive(Debug, Clone, PartialEq, Eq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Parts {
    /// What stands before the time: indentation, a bullet, an opening bracket.
    pub lead: String,
    /// The time as written, without brackets; empty when the line has none.
    pub time: String,
    /// What stands between the time and the rest: a closing bracket, whitespace, a separator.
    pub gap: String,
    /// Everything after that.
    pub rest: String,
}

/// The time of a line (R13.2.5).
///
/// ```
/// use trackparse::tracklist::parse_tracklist;
///
/// let list = parse_tracklist("[1:02:03] A - B\n1:02:03.5 C - D");
/// let first = list.entries[0].time.as_ref().unwrap();
/// assert_eq!((first.raw.as_str(), first.seconds), ("1:02:03", 3723.0));
/// let second = list.entries[1].time.as_ref().unwrap();
/// assert_eq!((second.raw.as_str(), second.seconds), ("1:02:03.5", 3723.5));
/// assert_eq!(first.to_json(), r#"{"raw":"1:02:03","seconds":3723}"#);
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct TimeValue {
    /// As written, without brackets.
    pub raw: String,
    /// The time in seconds; it may carry a fraction. A whole value is an integer in JSON.
    #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::seconds"))]
    pub seconds: f64,
}

/// One line of a tracklist.
///
/// ```
/// use trackparse::tracklist::{parse_tracklist, EntryKind, Eol};
///
/// let list = parse_tracklist("00:00 Noisia - Stigma\nw/ Phace - Cold Champagne\n");
/// let entry = &list.entries[1];
/// assert_eq!(entry.line, 2);
/// assert_eq!(entry.kind, EntryKind::Track);
/// assert_eq!(entry.eol, Eol::Lf);
/// assert_eq!(entry.raw, "w/ Phace - Cold Champagne");
/// assert!(entry.time.is_none());
/// assert!(entry.layered);
/// assert_eq!(entry.track.as_ref().unwrap().title, "Cold Champagne");
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct TracklistEntry {
    /// 1-based line number.
    pub line: usize,
    /// What the line is.
    pub kind: EntryKind,
    /// The line terminator that followed it.
    pub eol: Eol,
    /// The line as written, without its terminator. Never normalized.
    pub raw: String,
    /// `raw` cut into lead, time, gap and rest.
    pub parts: Parts,
    /// The line's time, if it has a valid one.
    pub time: Option<TimeValue>,
    /// The parse of the line text, for `Track` and `Marker` entries.
    pub track: Option<ParsedTrack>,
    /// The line starts with `w/`: played over the previous track (R13.13).
    pub layered: bool,
}

string_enum! {
    /// The code of a tracklist warning (R13.7), in the table order of the spec.
    ///
    /// ```
    /// use trackparse::tracklist::{parse_tracklist, WarningCode};
    ///
    /// let list = parse_tracklist("05:00 A - B\n03:00 C - D\nE - F");
    /// let codes: Vec<WarningCode> = list.warnings.iter().map(|w| w.code).collect();
    /// assert_eq!(
    ///     codes,
    ///     [WarningCode::NoStartAtZero, WarningCode::TimeOutOfOrder, WarningCode::TimeMissing]
    /// );
    /// assert_eq!(WarningCode::TimeOutOfOrder.as_str(), "time-out-of-order");
    /// ```
    #[non_exhaustive]
    WarningCode {
        TimeMissing => "time-missing", TimeOutOfOrder => "time-out-of-order",
        TimeDuplicate => "time-duplicate", NoStartAtZero => "no-start-at-zero",
        TimeAfterAudioEnd => "time-after-audio-end", InvalidTime => "invalid-time",
        PositionGap => "position-gap", PositionDuplicate => "position-duplicate",
        LineUnparsed => "line-unparsed",
    }
}

string_enum! {
    /// How serious a tracklist warning is.
    ///
    /// ```
    /// use trackparse::tracklist::{parse_tracklist, Severity};
    ///
    /// let list = parse_tracklist("05:00 A - B\n03:00 C - D");
    /// let severities: Vec<Severity> = list.warnings.iter().map(|w| w.severity).collect();
    /// assert_eq!(severities, [Severity::Info, Severity::Warning]);
    /// ```
    #[non_exhaustive]
    Severity { Info => "info", Warning => "warning" }
}

/// A fix for a `time-out-of-order` warning that changes one or two digits (R13.7).
///
/// ```
/// use trackparse::tracklist::parse_tracklist;
///
/// let text = "00:00 A - B\n01:02:00 C - D\n01:22:21 E - F\n01:06:40 G - H\n01:10:00 I - J";
/// let list = parse_tracklist(text);
/// assert_eq!(list.warnings[0].line, 4);
/// let suggestion = list.warnings[0].suggestion.as_ref().unwrap();
/// assert_eq!(suggestion.line, 3);
/// assert_eq!(suggestion.raw, "01:02:21");
/// assert_eq!(suggestion.seconds, 3741.0);
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct Suggestion {
    /// The line to change.
    pub line: usize,
    /// The time to write there.
    pub raw: String,
    /// That time in seconds.
    #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::seconds"))]
    pub seconds: f64,
}

/// A warning about a tracklist (R13.7).
///
/// ```
/// use trackparse::tracklist::{parse_tracklist, Severity, WarningCode};
///
/// let list = parse_tracklist("00:00 A - B\n00:00 C - D");
/// let warning = &list.warnings[0];
/// assert_eq!(warning.code, WarningCode::TimeDuplicate);
/// assert_eq!(warning.line, 2);
/// assert_eq!(warning.severity, Severity::Warning);
/// assert!(warning.suggestion.is_none());
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct TracklistWarning {
    /// What is wrong.
    pub code: WarningCode,
    /// The 1-based line it is about.
    pub line: usize,
    /// How serious it is.
    pub severity: Severity,
    /// A fix, for `time-out-of-order` when exactly one small change repairs the order.
    pub suggestion: Option<Suggestion>,
}

/// Counts over a tracklist.
///
/// ```
/// use trackparse::tracklist::parse_tracklist;
///
/// let stats = parse_tracklist("Tracklist:\n00:00 Intro\n02:10 Noisia - Stigma\nA - B").stats;
/// assert_eq!((stats.lines, stats.tracks, stats.markers, stats.timed), (4, 2, 1, 2));
/// assert_eq!(stats.first_time, Some(0.0));
/// assert_eq!(stats.last_time, Some(130.0));
/// ```
#[derive(Debug, Clone, PartialEq, Default)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "camelCase"))]
pub struct Stats {
    /// Number of lines (entries).
    pub lines: usize,
    /// Number of `Track` entries.
    pub tracks: usize,
    /// Number of `Marker` entries.
    pub markers: usize,
    /// Number of entries with a time.
    pub timed: usize,
    /// The time of the first timed entry, in seconds.
    #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::opt_seconds"))]
    pub first_time: Option<f64>,
    /// The time of the last timed entry, in seconds.
    #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::opt_seconds"))]
    pub last_time: Option<f64>,
}

/// The result of [`parse_tracklist`](super::parse_tracklist): one entry per line, the warnings
/// and the counts.
///
/// The entries' `raw` and `eol`, joined, are the text again (R13.1.2).
///
/// ```
/// use trackparse::tracklist::parse_tracklist;
///
/// let text = "00:00 Noisia - Stigma\r\n03:45 Phace - Cold Champagne";
/// let list = parse_tracklist(text);
/// assert_eq!(list.spec_version, trackparse::SPEC_VERSION);
/// assert_eq!(list.entries.len(), 2);
/// let joined: String = list.entries.iter().map(|e| format!("{}{}", e.raw, e.eol)).collect();
/// assert_eq!(joined, text);
/// assert!(list.to_json().starts_with(r#"{"specVersion":""#));
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(rename_all = "camelCase"))]
pub struct Tracklist {
    /// The version of the spec the result follows (`specVersion` in JSON).
    pub spec_version: String,
    /// One entry per line, in order.
    pub entries: Vec<TracklistEntry>,
    /// Warnings, ordered by line.
    pub warnings: Vec<TracklistWarning>,
    /// Counts.
    pub stats: Stats,
}

/// One edit of a tracklist text (R13.9). Line numbers are 1-based and refer to the text as the
/// previous edit left it.
///
/// The JSON form is the spec's (`{"op":"set-time","line":3,"seconds":90}`).
///
/// ```
/// use trackparse::tracklist::{apply_edits, Edit};
///
/// let text = "00:00 A - B\n01:00 C - D\n";
/// let edits = [Edit::set_time(2, 90.0), Edit::insert(2, "02:00 E - F"), Edit::delete(1)];
/// assert_eq!(apply_edits(text, &edits).text, "01:30 C - D\n02:00 E - F\n");
/// assert_eq!(edits[0].to_json(), r#"{"op":"set-time","line":2,"seconds":90}"#);
/// assert_eq!(edits[0], Edit::SetTime { line: 2, seconds: 90.0 });
/// ```
#[derive(Debug, Clone, PartialEq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
#[cfg_attr(feature = "serde", serde(tag = "op", rename_all = "kebab-case"))]
#[non_exhaustive]
pub enum Edit {
    /// Rewrite the time of `line`, keeping its written style (R13.9.2).
    SetTime {
        /// The line to change.
        line: usize,
        /// The new time in seconds.
        #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::seconds"))]
        seconds: f64,
    },
    /// Add `seconds` to every timed line of `lines`, else from line `from` on, else of the
    /// whole text.
    Shift {
        /// Seconds to add; negative moves the times back.
        #[cfg_attr(feature = "serde", serde(serialize_with = "crate::serde_support::seconds"))]
        seconds: f64,
        /// Only these lines.
        #[cfg_attr(feature = "serde", serde(default, skip_serializing_if = "Option::is_none"))]
        lines: Option<Vec<usize>>,
        /// Only this line and the ones after it.
        #[cfg_attr(feature = "serde", serde(default, skip_serializing_if = "Option::is_none"))]
        from: Option<usize>,
    },
    /// Stable sort of the timed lines, each with the untimed lines after it (R13.9.4).
    SortByTime,
    /// Replace everything after the time and gap of `line`.
    SetRest {
        /// The line to change.
        line: usize,
        /// The new text after the time.
        rest: String,
    },
    /// Insert `text` as a new line after line `after` (0: at the start).
    Insert {
        /// The line to insert after.
        after: usize,
        /// The new line, without a terminator.
        text: String,
    },
    /// Remove `line`.
    Delete {
        /// The line to remove.
        line: usize,
    },
}

impl Edit {
    /// [`Edit::SetTime`]: rewrite the time of `line`.
    pub fn set_time(line: usize, seconds: f64) -> Edit {
        Edit::SetTime { line, seconds }
    }

    /// [`Edit::Shift`] over every timed line of the text.
    ///
    /// ```
    /// use trackparse::tracklist::{apply_edits, Edit};
    ///
    /// let result = apply_edits("00:10 A - B\n01:00 C - D", &[Edit::shift(-10.0)]);
    /// assert_eq!(result.text, "00:00 A - B\n00:50 C - D");
    /// ```
    pub fn shift(seconds: f64) -> Edit {
        Edit::Shift { seconds, lines: None, from: None }
    }

    /// [`Edit::Shift`] over line `from` and the lines after it.
    pub fn shift_from(seconds: f64, from: usize) -> Edit {
        Edit::Shift { seconds, lines: None, from: Some(from) }
    }

    /// [`Edit::Shift`] over the given lines only.
    pub fn shift_lines(seconds: f64, lines: impl IntoIterator<Item = usize>) -> Edit {
        Edit::Shift { seconds, lines: Some(lines.into_iter().collect()), from: None }
    }

    /// [`Edit::SortByTime`].
    pub fn sort_by_time() -> Edit {
        Edit::SortByTime
    }

    /// [`Edit::SetRest`]: replace what follows the time of `line`.
    pub fn set_rest(line: usize, rest: impl Into<String>) -> Edit {
        Edit::SetRest { line, rest: rest.into() }
    }

    /// [`Edit::Insert`]: a new line after line `after` (0: at the start).
    pub fn insert(after: usize, text: impl Into<String>) -> Edit {
        Edit::Insert { after, text: text.into() }
    }

    /// [`Edit::Delete`]: remove `line`.
    pub fn delete(line: usize) -> Edit {
        Edit::Delete { line }
    }
}

string_enum! {
    /// The code of an edit warning (R13.9).
    ///
    /// ```
    /// use trackparse::tracklist::{apply_edits, Edit, EditWarningCode};
    ///
    /// let result = apply_edits("00:10 A - B", &[Edit::shift(-60.0), Edit::delete(9)]);
    /// let codes: Vec<EditWarningCode> = result.warnings.iter().map(|w| w.code).collect();
    /// assert_eq!(codes, [EditWarningCode::TimeClamped, EditWarningCode::EditInvalid]);
    /// ```
    #[non_exhaustive]
    EditWarningCode {
        EditInvalid => "edit-invalid", TimeClamped => "time-clamped",
        StyleUpgraded => "style-upgraded",
    }
}

/// A warning about one edit.
///
/// ```
/// use trackparse::tracklist::{apply_edits, Edit, EditWarningCode};
///
/// let result = apply_edits("59:30 A - B", &[Edit::shift(60.0)]);
/// assert_eq!(result.text, "01:00:30 A - B");
/// let warning = &result.warnings[0];
/// assert_eq!(warning.code, EditWarningCode::StyleUpgraded);
/// assert_eq!(warning.edit, 0);
/// assert_eq!(warning.line, Some(1));
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct EditWarning {
    /// What happened.
    pub code: EditWarningCode,
    /// Index of the edit in the `edits` slice.
    pub edit: usize,
    /// The line concerned; for `EditInvalid` the edit's own `line` (or `after`), which may not
    /// exist in the text.
    pub line: Option<i64>,
}

/// The result of [`apply_edits`](super::apply_edits).
///
/// ```
/// use trackparse::tracklist::{apply_edits, Edit};
///
/// let result = apply_edits("00:00 A - B\n01:00 C - D", &[Edit::set_rest(2, "E - F")]);
/// assert_eq!(result.text, "00:00 A - B\n01:00 E - F");
/// assert_eq!(result.changed, [2]);
/// assert!(result.warnings.is_empty());
/// assert_eq!(
///     result.to_json(),
///     r#"{"text":"00:00 A - B\n01:00 E - F","changed":[2],"warnings":[]}"#
/// );
/// ```
#[derive(Debug, Clone, PartialEq, Eq)]
#[cfg_attr(feature = "serde", derive(serde::Serialize, serde::Deserialize))]
pub struct EditResult {
    /// The edited text.
    pub text: String,
    /// Line numbers (in the final text) of lines an edit wrote; sorted, unique.
    pub changed: Vec<usize>,
    /// One warning per edit that could not be applied as asked.
    pub warnings: Vec<EditWarning>,
}

json_methods!(
    Parts,
    TimeValue,
    TracklistEntry,
    Suggestion,
    TracklistWarning,
    Stats,
    Tracklist,
    Edit,
    EditWarning,
    EditResult
);

pub(crate) fn num(n: usize) -> Json {
    Json::Num(n as f64)
}

fn num_opt(v: Option<f64>) -> Json {
    v.map_or(Json::Null, Json::Num)
}

impl ToJson for Parts {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("lead", Json::str(&self.lead)),
            ("time", Json::str(&self.time)),
            ("gap", Json::str(&self.gap)),
            ("rest", Json::str(&self.rest)),
        ])
    }
}

impl ToJson for TimeValue {
    fn to_json_value(&self) -> Json {
        obj(vec![("raw", Json::str(&self.raw)), ("seconds", Json::Num(self.seconds))])
    }
}

impl TracklistEntry {
    /// The entry's JSON with `track` given: the wire writes the pipeline's own track there.
    pub(crate) fn json_with(&self, track: Json) -> Json {
        obj(vec![
            ("line", num(self.line)),
            ("kind", Json::str(self.kind.as_str())),
            ("eol", Json::str(self.eol.as_str())),
            ("raw", Json::str(&self.raw)),
            ("parts", self.parts.to_json_value()),
            ("time", opt(&self.time)),
            ("track", track),
            ("layered", Json::Bool(self.layered)),
        ])
    }
}

impl ToJson for TracklistEntry {
    fn to_json_value(&self) -> Json {
        self.json_with(opt(&self.track))
    }
}

impl ToJson for Suggestion {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("line", num(self.line)),
            ("raw", Json::str(&self.raw)),
            ("seconds", Json::Num(self.seconds)),
        ])
    }
}

impl ToJson for TracklistWarning {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("code", Json::str(self.code.as_str())),
            ("line", num(self.line)),
            ("severity", Json::str(self.severity.as_str())),
            ("suggestion", opt(&self.suggestion)),
        ])
    }
}

impl ToJson for Stats {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("lines", num(self.lines)),
            ("tracks", num(self.tracks)),
            ("markers", num(self.markers)),
            ("timed", num(self.timed)),
            ("firstTime", num_opt(self.first_time)),
            ("lastTime", num_opt(self.last_time)),
        ])
    }
}

/// The JSON of a tracklist whose entries are already written.
pub(crate) fn tracklist_json(
    spec_version: &str,
    entries: Vec<Json>,
    warnings: &[TracklistWarning],
    stats: &Stats,
) -> Json {
    obj(vec![
        ("specVersion", Json::str(spec_version)),
        ("entries", Json::Arr(entries)),
        ("warnings", list(warnings)),
        ("stats", stats.to_json_value()),
    ])
}

impl ToJson for Tracklist {
    fn to_json_value(&self) -> Json {
        let entries = self.entries.iter().map(ToJson::to_json_value).collect();
        tracklist_json(&self.spec_version, entries, &self.warnings, &self.stats)
    }
}

impl ToJson for Edit {
    /// The edit as the spec writes it (`{"op":"set-time","line":3,"seconds":90}`).
    fn to_json_value(&self) -> Json {
        match self {
            Edit::SetTime { line, seconds } => obj(vec![
                ("op", Json::str("set-time")),
                ("line", num(*line)),
                ("seconds", Json::Num(*seconds)),
            ]),
            Edit::Shift { seconds, lines, from } => {
                let mut entries =
                    vec![("op", Json::str("shift")), ("seconds", Json::Num(*seconds))];
                if let Some(lines) = lines {
                    entries.push(("lines", Json::Arr(lines.iter().map(|l| num(*l)).collect())));
                }
                if let Some(from) = from {
                    entries.push(("from", num(*from)));
                }
                obj(entries)
            }
            Edit::SortByTime => obj(vec![("op", Json::str("sort-by-time"))]),
            Edit::SetRest { line, rest } => obj(vec![
                ("op", Json::str("set-rest")),
                ("line", num(*line)),
                ("rest", Json::str(rest)),
            ]),
            Edit::Insert { after, text } => obj(vec![
                ("op", Json::str("insert")),
                ("after", num(*after)),
                ("text", Json::str(text)),
            ]),
            Edit::Delete { line } => obj(vec![("op", Json::str("delete")), ("line", num(*line))]),
        }
    }
}

impl ToJson for EditWarning {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("code", Json::str(self.code.as_str())),
            ("edit", num(self.edit)),
            ("line", self.line.map_or(Json::Null, |l| Json::Num(l as f64))),
        ])
    }
}

impl ToJson for EditResult {
    fn to_json_value(&self) -> Json {
        obj(vec![
            ("text", Json::str(&self.text)),
            ("changed", Json::Arr(self.changed.iter().map(|l| num(*l)).collect())),
            ("warnings", list(&self.warnings)),
        ])
    }
}
