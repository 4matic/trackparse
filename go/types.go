package trackparse

// Public types. They mirror spec/schema/parsed-track.schema.json and
// spec/schema/tracklist.schema.json: field names are Go-cased here and the spec's camelCase in
// JSON (see MarshalJSON in json.go). Every key is always present in the JSON form, nullable
// fields are pointers, and enums are typed strings that hold the spec's names.

// Mode is the mode a string is parsed in (R2). In a [Track] it is clean, youtube, filename or
// tracklist. In [ParseOptions] and [TracklistOptions] the zero value [ModeAuto] asks the parser
// to pick one; auto never resolves to tracklist (R13.6).
type Mode string

// The modes. Any other string in an option behaves as the JavaScript package does with an
// unknown mode: no youtube detection, filenames still detected.
const (
	ModeAuto      Mode = ""
	ModeClean     Mode = "clean"
	ModeYouTube   Mode = "youtube"
	ModeFilename  Mode = "filename"
	ModeTracklist Mode = "tracklist"
)

// AllModes lists the modes a [Track] can have, in the spec's order.
var AllModes = []Mode{ModeClean, ModeYouTube, ModeFilename, ModeTracklist}

// Role is what an artist did on the track (R7).
type Role string

// The artist roles.
const (
	RolePrimary  Role = "primary"
	RoleFeatured Role = "featured"
	RoleRemixer  Role = "remixer"
	RoleProducer Role = "producer"
)

// AllRoles lists every [Role], in the spec's order.
var AllRoles = []Role{RolePrimary, RoleFeatured, RoleRemixer, RoleProducer}

// Source is where in the string an artist was found: the artist side, the title side, or a
// version group.
type Source string

// The artist sources.
const (
	SourceArtist  Source = "artist"
	SourceTitle   Source = "title"
	SourceVersion Source = "version"
)

// AllSources lists every [Source], in the spec's order.
var AllSources = []Source{SourceArtist, SourceTitle, SourceVersion}

// VersionType is the type of a version (R5): remix, edit, live, remaster and so on. A version
// head added through [Keywords] can carry any string, which the output keeps as given.
type VersionType string

// The version types of the spec.
const (
	VersionTypeRemix        VersionType = "remix"
	VersionTypeBootleg      VersionType = "bootleg"
	VersionTypeVIP          VersionType = "vip"
	VersionTypeEdit         VersionType = "edit"
	VersionTypeFlip         VersionType = "flip"
	VersionTypeRefix        VersionType = "refix"
	VersionTypeRework       VersionType = "rework"
	VersionTypeMashup       VersionType = "mashup"
	VersionTypeBlend        VersionType = "blend"
	VersionTypeDub          VersionType = "dub"
	VersionTypeMix          VersionType = "mix"
	VersionTypeExtended     VersionType = "extended"
	VersionTypeRadio        VersionType = "radio"
	VersionTypeClub         VersionType = "club"
	VersionTypeOriginal     VersionType = "original"
	VersionTypeInstrumental VersionType = "instrumental"
	VersionTypeAcapella     VersionType = "acapella"
	VersionTypeLive         VersionType = "live"
	VersionTypeAcoustic     VersionType = "acoustic"
	VersionTypeRemaster     VersionType = "remaster"
	VersionTypeDemo         VersionType = "demo"
	VersionTypeReprise      VersionType = "reprise"
	VersionTypeCover        VersionType = "cover"
	VersionTypeVersion      VersionType = "version"
	VersionTypeSpedUp       VersionType = "spedUp"
	VersionTypeSlowed       VersionType = "slowed"
	VersionTypeNightcore    VersionType = "nightcore"
)

// AllVersionTypes lists the 27 version types of the spec, in the spec's order.
var AllVersionTypes = []VersionType{
	VersionTypeRemix, VersionTypeBootleg, VersionTypeVIP, VersionTypeEdit, VersionTypeFlip,
	VersionTypeRefix, VersionTypeRework, VersionTypeMashup, VersionTypeBlend, VersionTypeDub,
	VersionTypeMix, VersionTypeExtended, VersionTypeRadio, VersionTypeClub, VersionTypeOriginal,
	VersionTypeInstrumental, VersionTypeAcapella, VersionTypeLive, VersionTypeAcoustic,
	VersionTypeRemaster, VersionTypeDemo, VersionTypeReprise, VersionTypeCover,
	VersionTypeVersion, VersionTypeSpedUp, VersionTypeSlowed, VersionTypeNightcore,
}

// Delimiter is how a version was written: in a bracket group, or after a dash.
type Delimiter string

// The version delimiters.
const (
	DelimiterParen  Delimiter = "("
	DelimiterSquare Delimiter = "["
	DelimiterCurly  Delimiter = "{"
	DelimiterDash   Delimiter = "-"
)

// AllDelimiters lists every [Delimiter], in the spec's order.
var AllDelimiters = []Delimiter{DelimiterParen, DelimiterSquare, DelimiterCurly, DelimiterDash}

// JunkKind is what kind of noise a junk fragment is (R5). A junk phrase added through
// [Keywords] can carry any string, which the output keeps as given.
type JunkKind string

// The junk kinds of the spec.
const (
	JunkKindVideo    JunkKind = "video"
	JunkKindAudio    JunkKind = "audio"
	JunkKindLyrics   JunkKind = "lyrics"
	JunkKindQuality  JunkKind = "quality"
	JunkKindPromo    JunkKind = "promo"
	JunkKindPlatform JunkKind = "platform"
	JunkKindLabel    JunkKind = "label"
	JunkKindGenre    JunkKind = "genre"
	JunkKindOther    JunkKind = "other"
)

// AllJunkKinds lists the junk kinds of the spec, in the spec's order.
var AllJunkKinds = []JunkKind{
	JunkKindVideo, JunkKindAudio, JunkKindLyrics, JunkKindQuality, JunkKindPromo,
	JunkKindPlatform, JunkKindLabel, JunkKindGenre, JunkKindOther,
}

// Warning is a warning of the single-track parser: the result is a guess the caller may want
// to check.
type Warning string

// The track warnings.
const (
	WarningNoSeparator         Warning = "noSeparator"
	WarningAmbiguousSeparator  Warning = "ambiguousSeparator"
	WarningUnspacedDashSplit   Warning = "unspacedDashSplit"
	WarningAsymmetricDashSplit Warning = "asymmetricDashSplit"
	WarningBySplit             Warning = "bySplit"
	WarningQuotedTitleSplit    Warning = "quotedTitleSplit"
	WarningAmbiguousMixCredit  Warning = "ambiguousMixCredit"
	WarningUnbalancedBrackets  Warning = "unbalancedBrackets"
)

// AllWarnings lists every [Warning], in the spec's order.
var AllWarnings = []Warning{
	WarningNoSeparator, WarningAmbiguousSeparator, WarningUnspacedDashSplit,
	WarningAsymmetricDashSplit, WarningBySplit, WarningQuotedTitleSplit,
	WarningAmbiguousMixCredit, WarningUnbalancedBrackets,
}

// Artist is one credited artist.
type Artist struct {
	// Name is the name as written, trimmed.
	Name string
	// Role is what the artist did on the track.
	Role Role
	// Joiner is the raw joiner or marker text that preceded the name in its credit list; nil
	// for the first name of a list.
	Joiner *string
	// Source is where in the string the artist was found.
	Source Source
}

// Version is one version of the track: a remix, an edit, a live take, a remaster (R5, R8).
type Version struct {
	// Type is the version type.
	Type VersionType
	// Raw is the version text as written, without its brackets.
	Raw string
	// Artists are who made it (role remixer); empty for versions without a credit.
	Artists []Artist
	// Modifiers are the other version words of the group, as lowercase keys ("extended" in
	// "Extended Remix").
	Modifiers []string
	// Descriptor is free text of the version that is neither a credit nor a keyword.
	Descriptor *string
	// Year is a year written inside the version.
	Year *int
	// UnknownArtist is true when the credit is a placeholder such as ID or Unknown.
	UnknownArtist bool
	// Delimiter is how the version was written.
	Delimiter Delimiter
}

// Junk is a fragment that says nothing about the track: Official Video, HD, Free Download. It
// is taken out of the title and kept here (the parser extracts, it does not delete).
type Junk struct {
	// Raw is the fragment as written, without its brackets.
	Raw string
	// Kind is what kind of noise it is.
	Kind JunkKind
}

// Flags are yes/no facts about the track.
type Flags struct {
	// Explicit is true when the track is marked Explicit or Dirty.
	Explicit bool
	// Clean is true when the track is marked Clean.
	Clean bool
	// UnknownArtist is true when the artist is a placeholder (ID, Unknown Artist).
	UnknownArtist bool
	// UnknownTitle is true when the title is a placeholder (ID, Untitled).
	UnknownTitle bool
}

// Position is a track number in front of the string (R4.2).
type Position struct {
	// Raw is the number as written, without the separator after it.
	Raw string
	// Number is its value.
	Number int
}

// Timestamp is a cue time in front of a single track string (R4.1), in whole seconds.
type Timestamp struct {
	// Raw is the time as written, without brackets.
	Raw string
	// Seconds is the time in seconds.
	Seconds int
}

// Track is the result of [Parse]: everything recognized in one track string. Anything
// recognized is in a field; anything not recognized stays in Title verbatim.
type Track struct {
	// Input is the string that was parsed, unchanged.
	Input string
	// Mode is the mode it was parsed in; never ModeAuto.
	Mode Mode
	// Position is a leading track number.
	Position *Position
	// Timestamp is a leading cue time.
	Timestamp *Timestamp
	// Artists are the primary, featured and producer credits, in the order written. Remixers
	// are in Versions; [AllArtists] gives both.
	Artists []Artist
	// Title is the title without versions, featured credits and junk.
	Title string
	// FullTitle is the title with its versions, as written.
	FullTitle string
	// Versions are the remixes, edits and other versions, in the order written.
	Versions []Version
	// Year is a release year found outside the versions.
	Year *int
	// Flags are yes/no facts.
	Flags Flags
	// Junk are the fragments that say nothing about the track.
	Junk []Junk
	// Warnings are guesses the caller may want to check.
	Warnings []Warning
}

// ---------------------------------------------------------------------------------------------
// Tracklists (R13)

// EntryKind is what a line of a tracklist is (R13.3).
type EntryKind string

// The entry kinds.
const (
	EntryKindTrack  EntryKind = "track"
	EntryKindMarker EntryKind = "marker"
	EntryKindHeader EntryKind = "header"
	EntryKindLink   EntryKind = "link"
	EntryKindNote   EntryKind = "note"
	EntryKindBlank  EntryKind = "blank"
)

// AllEntryKinds lists every [EntryKind], in the spec's order.
var AllEntryKinds = []EntryKind{
	EntryKindTrack, EntryKindMarker, EntryKindHeader, EntryKindLink, EntryKindNote,
	EntryKindBlank,
}

// Eol is the line terminator that followed a line (R13.1.1). The value is the terminator
// itself; [EolNone] is the empty string, for a last line without one.
type Eol string

// The line terminators.
const (
	EolLF   Eol = "\n"
	EolCRLF Eol = "\r\n"
	EolCR   Eol = "\r"
	EolNone Eol = ""
)

// AllEols lists every [Eol], in the spec's order.
var AllEols = []Eol{EolLF, EolCRLF, EolCR, EolNone}

// Parts is a line cut into four pieces that join back to it: Lead + Time + Gap + Rest is the
// raw line (R13.1.3).
type Parts struct {
	// Lead is what stands before the time: indentation, a bullet, an opening bracket.
	Lead string
	// Time is the time as written, without brackets; empty when the line has none.
	Time string
	// Gap is what stands between the time and the rest: a closing bracket, whitespace, a
	// separator.
	Gap string
	// Rest is everything after that.
	Rest string
}

// TracklistTime is the time of a tracklist line (R13.2.5).
type TracklistTime struct {
	// Raw is the time as written, without brackets.
	Raw string
	// Seconds is the time in seconds; it may carry a fraction. A whole value is an integer in
	// JSON.
	Seconds float64
}

// Entry is one line of a tracklist.
type Entry struct {
	// Line is the 1-based line number.
	Line int
	// Kind is what the line is.
	Kind EntryKind
	// Eol is the line terminator that followed it.
	Eol Eol
	// Raw is the line as written, without its terminator. Never normalized.
	Raw string
	// Parts is Raw cut into lead, time, gap and rest.
	Parts Parts
	// Time is the line's time, if it has a valid one.
	Time *TracklistTime
	// Track is the parse of the line text, for track and marker entries.
	Track *Track
	// Layered is true when the line starts with w/: played over the previous track (R13.13).
	Layered bool
}

// WarningCode is the code of a tracklist warning (R13.7).
type WarningCode string

// The tracklist warning codes, in the table order of the spec.
const (
	WarningCodeTimeMissing       WarningCode = "time-missing"
	WarningCodeTimeOutOfOrder    WarningCode = "time-out-of-order"
	WarningCodeTimeDuplicate     WarningCode = "time-duplicate"
	WarningCodeNoStartAtZero     WarningCode = "no-start-at-zero"
	WarningCodeTimeAfterAudioEnd WarningCode = "time-after-audio-end"
	WarningCodeInvalidTime       WarningCode = "invalid-time"
	WarningCodePositionGap       WarningCode = "position-gap"
	WarningCodePositionDuplicate WarningCode = "position-duplicate"
	WarningCodeLineUnparsed      WarningCode = "line-unparsed"
)

// AllWarningCodes lists every [WarningCode], in the spec's order.
var AllWarningCodes = []WarningCode{
	WarningCodeTimeMissing, WarningCodeTimeOutOfOrder, WarningCodeTimeDuplicate,
	WarningCodeNoStartAtZero, WarningCodeTimeAfterAudioEnd, WarningCodeInvalidTime,
	WarningCodePositionGap, WarningCodePositionDuplicate, WarningCodeLineUnparsed,
}

// Severity is how serious a tracklist warning is.
type Severity string

// The severities.
const (
	SeverityInfo    Severity = "info"
	SeverityWarning Severity = "warning"
)

// AllSeverities lists every [Severity], in the spec's order.
var AllSeverities = []Severity{SeverityInfo, SeverityWarning}

// Suggestion is a fix for a time-out-of-order warning that changes one or two digits (R13.7).
type Suggestion struct {
	// Line is the line to change.
	Line int
	// Raw is the time to write there.
	Raw string
	// Seconds is that time in seconds.
	Seconds float64
}

// TracklistWarning is a warning about a tracklist (R13.7).
type TracklistWarning struct {
	// Code is what is wrong.
	Code WarningCode
	// Line is the 1-based line it is about.
	Line int
	// Severity is how serious it is.
	Severity Severity
	// Suggestion is a fix, for time-out-of-order when exactly one small change repairs the
	// order.
	Suggestion *Suggestion
}

// Stats are counts over a tracklist.
type Stats struct {
	// Lines is the number of lines (entries).
	Lines int
	// Tracks is the number of track entries.
	Tracks int
	// Markers is the number of marker entries.
	Markers int
	// Timed is the number of entries with a time.
	Timed int
	// FirstTime is the time of the first timed entry, in seconds.
	FirstTime *float64
	// LastTime is the time of the last timed entry, in seconds.
	LastTime *float64
}

// Tracklist is the result of [ParseTracklist]: one entry per line, the warnings and the
// counts. The entries' Raw and Eol, joined, are the text again (R13.1.2).
type Tracklist struct {
	// SpecVersion is the version of the spec the result follows.
	SpecVersion string
	// Entries has one entry per line, in order.
	Entries []Entry
	// Warnings are ordered by line.
	Warnings []TracklistWarning
	// Stats are the counts.
	Stats Stats
}

// EditOp names the operation of an [Edit] (R13.9).
type EditOp string

// The edit operations.
const (
	EditOpSetTime    EditOp = "set-time"
	EditOpShift      EditOp = "shift"
	EditOpSortByTime EditOp = "sort-by-time"
	EditOpSetRest    EditOp = "set-rest"
	EditOpInsert     EditOp = "insert"
	EditOpDelete     EditOp = "delete"
)

// AllEditOps lists every [EditOp], in the spec's order.
var AllEditOps = []EditOp{
	EditOpSetTime, EditOpShift, EditOpSortByTime, EditOpSetRest, EditOpInsert, EditOpDelete,
}

// Edit is one edit of a tracklist text (R13.9). Line numbers are 1-based and refer to the text
// as the previous edit left it. Build one with [SetTime], [Shift], [ShiftFrom], [ShiftLines],
// [SortByTime], [SetRest], [Insert] or [Delete]; each operation reads only its own fields. An
// edit that cannot be applied changes nothing and is reported as [EditWarningCodeEditInvalid].
type Edit struct {
	// Op is the operation.
	Op EditOp
	// Line is the line to change (set-time, set-rest, delete).
	Line int
	// Seconds is the new time (set-time) or the amount to add (shift; negative moves back).
	Seconds float64
	// Lines limits a shift to these lines. Nil means not given; an empty non-nil slice shifts
	// no line.
	Lines []int
	// From limits a shift to this line and the ones after it. Nil means not given.
	From *int
	// Rest is the new text after the time and gap (set-rest).
	Rest string
	// After is the line to insert after; 0 inserts at the start (insert).
	After int
	// Text is the new line, without a terminator (insert).
	Text string
}

// EditWarningCode is the code of an edit warning (R13.9).
type EditWarningCode string

// The edit warning codes.
const (
	EditWarningCodeEditInvalid   EditWarningCode = "edit-invalid"
	EditWarningCodeTimeClamped   EditWarningCode = "time-clamped"
	EditWarningCodeStyleUpgraded EditWarningCode = "style-upgraded"
)

// AllEditWarningCodes lists every [EditWarningCode], in the spec's order.
var AllEditWarningCodes = []EditWarningCode{
	EditWarningCodeEditInvalid, EditWarningCodeTimeClamped, EditWarningCodeStyleUpgraded,
}

// EditWarning is a warning about one edit.
type EditWarning struct {
	// Code is what happened.
	Code EditWarningCode
	// Edit is the index of the edit in the edits slice.
	Edit int
	// Line is the line concerned; for edit-invalid the edit's own line (or after), which may
	// not exist in the text.
	Line *int
}

// EditResult is the result of [ApplyEdits].
type EditResult struct {
	// Text is the edited text.
	Text string
	// Changed are the line numbers (in the final text) of lines an edit wrote; sorted, unique.
	Changed []int
	// Warnings has one warning per edit that could not be applied as asked.
	Warnings []EditWarning
}

// ---------------------------------------------------------------------------------------------
// Internal forms

// rawEditWarning is an edit warning with the line as the JS reference holds it: any number an
// invalid edit carried (behind the JSON door it may be negative or fractional), not only a
// real line number.
type rawEditWarning struct {
	code EditWarningCode
	edit int
	line *float64
}

// rawEditResult is the result of applyEditsRaw before it is typed or written as JSON.
type rawEditResult struct {
	text     string
	changed  []int
	warnings []rawEditWarning
}

// result is the typed form. Typed edits carry int lines, so every line is a whole number in
// int range; the conversion saturates anyway.
func (r rawEditResult) result() EditResult {
	out := EditResult{Text: r.text, Changed: r.changed, Warnings: make([]EditWarning, 0, len(r.warnings))}
	for _, w := range r.warnings {
		ew := EditWarning{Code: w.code, Edit: w.edit}
		if w.line != nil {
			n := saturatingInt(*w.line)
			ew.Line = &n
		}
		out.Warnings = append(out.Warnings, ew)
	}
	if out.Changed == nil {
		out.Changed = []int{}
	}
	return out
}

// saturatingInt converts a float to int, clamping to the int range (NaN gives 0).
func saturatingInt(f float64) int {
	const maxInt = int(^uint(0) >> 1)
	switch {
	case f != f:
		return 0
	case f >= float64(maxInt):
		return maxInt
	case f <= float64(-maxInt-1):
		return -maxInt - 1
	}
	return int(f)
}
