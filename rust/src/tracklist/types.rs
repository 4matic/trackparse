//! Public tracklist types. They mirror `spec/schema/tracklist.schema.json`; field names are
//! snake_case in Rust and the spec's camelCase in JSON (`to_json()`).

use crate::json::Json;
use crate::types::{list, obj, opt, string_enum, ParsedTrack, ToJson};

string_enum! {
    /// R13.3: what a line is.
    EntryKind {
        Track => "track", Marker => "marker", Header => "header", Link => "link", Note => "note",
        Blank => "blank",
    }
}

string_enum! {
    /// R13.1.1: the line terminator that followed a line (`None`: the text ended without one).
    Eol { Lf => "\n", CrLf => "\r\n", Cr => "\r", None => "" }
}

/// R13.1.3: `lead + time + gap + rest == raw`.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Parts {
    pub lead: String,
    pub time: String,
    pub gap: String,
    pub rest: String,
}

/// R13.2.5: `raw` without brackets; `seconds` may carry a fraction.
#[derive(Debug, Clone, PartialEq)]
pub struct TimeValue {
    pub raw: String,
    pub seconds: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TracklistEntry {
    /// 1-based line number.
    pub line: usize,
    pub kind: EntryKind,
    pub eol: Eol,
    pub raw: String,
    pub parts: Parts,
    pub time: Option<TimeValue>,
    /// The parse of the line text, for `Track` and `Marker` entries.
    pub track: Option<ParsedTrack>,
    /// R13.13: the line starts with `w/`: played over the previous track.
    pub layered: bool,
}

string_enum! {
    /// R13.7 warning codes, in the table order of the spec.
    WarningCode {
        TimeMissing => "time-missing", TimeOutOfOrder => "time-out-of-order",
        TimeDuplicate => "time-duplicate", NoStartAtZero => "no-start-at-zero",
        TimeAfterAudioEnd => "time-after-audio-end", InvalidTime => "invalid-time",
        PositionGap => "position-gap", PositionDuplicate => "position-duplicate",
        LineUnparsed => "line-unparsed",
    }
}

string_enum! {
    Severity { Info => "info", Warning => "warning" }
}

/// R13.7: a one- or two-digit fix for `time-out-of-order`; `line` is the line to change.
#[derive(Debug, Clone, PartialEq)]
pub struct Suggestion {
    pub line: usize,
    pub raw: String,
    pub seconds: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TracklistWarning {
    pub code: WarningCode,
    pub line: usize,
    pub severity: Severity,
    pub suggestion: Option<Suggestion>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Stats {
    pub lines: usize,
    pub tracks: usize,
    pub markers: usize,
    pub timed: usize,
    pub first_time: Option<f64>,
    pub last_time: Option<f64>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Tracklist {
    pub spec_version: String,
    pub entries: Vec<TracklistEntry>,
    pub warnings: Vec<TracklistWarning>,
    pub stats: Stats,
}

/// R13.9: one edit. Line numbers are 1-based and refer to the text as the previous edit left it.
#[derive(Debug, Clone, PartialEq)]
pub enum Edit {
    /// Rewrite the time of `line`, keeping its written style (R13.9.2).
    SetTime {
        line: usize,
        seconds: f64,
    },
    /// Add `seconds` to every timed line of `lines`, else from line `from` on, else of the text.
    Shift {
        seconds: f64,
        lines: Option<Vec<usize>>,
        from: Option<usize>,
    },
    /// R13.9.4: stable sort of the timed lines, each with the untimed lines after it.
    SortByTime,
    /// Replace everything after the time and gap of `line`.
    SetRest {
        line: usize,
        rest: String,
    },
    /// Insert `text` as a new line after line `after` (0: at the start).
    Insert {
        after: usize,
        text: String,
    },
    Delete {
        line: usize,
    },
}

string_enum! {
    EditWarningCode {
        EditInvalid => "edit-invalid", TimeClamped => "time-clamped",
        StyleUpgraded => "style-upgraded",
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditWarning {
    pub code: EditWarningCode,
    /// Index of the edit in the `edits` slice.
    pub edit: usize,
    /// The line concerned; for `EditInvalid` the edit's own `line` (or `after`), which may not
    /// exist in the text.
    pub line: Option<i64>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditResult {
    pub text: String,
    /// Line numbers (in the final text) of lines an edit wrote; sorted, unique.
    pub changed: Vec<usize>,
    pub warnings: Vec<EditWarning>,
}

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
