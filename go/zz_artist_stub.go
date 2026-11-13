package trackparse

// TEMPORARY: minimal stand-ins for the artist side (classify.go R5, artists.go R7, credits.go
// R7.3 to R7.5, assemble.go R9), so the pipeline compiles and can be tested before that port
// lands. Each symbol mirrors the Rust item named in its comment, Go-cased. Delete this file
// when the real files are merged. The stand-in behaviour is deliberately small: nothing is
// classified, and an artist side becomes one primary credit.

// versionDraft mirrors rust/src/classify.rs VersionDraft.
type versionDraft struct {
	// vtype is a version type name; keywords.versionHeads values pass through as given.
	vtype         string
	raw           string
	artists       []credit
	modifiers     []string
	descriptor    *string
	year          *int
	unknownArtist bool
	delimiter     Delimiter
	pos           int
	// ambiguousMix is R5.7.2 ambiguousMixCredit; emitted only when the version is used.
	ambiguousMix bool
	// prefixForm: recognized by the R5.7.3 prefix form (R6.2 condition (ii) does not apply).
	prefixForm bool
}

// flag mirrors rust/src/classify.rs Flag.
type flag uint8

const (
	flagExplicit flag = iota
	flagClean
)

// classKind is the variant of a classification (rust/src/classify.rs Classification).
type classKind uint8

const (
	classUnknown classKind = iota
	classJunk
	classFeat
	classProducer
	classFlag
	classYear
	classVersions
)

// classification mirrors rust/src/classify.rs Classification: kind says which field is set.
type classification struct {
	kind classKind
	// junk is the junk kind (classJunk).
	junk string
	// list is the credit list (classFeat, classProducer).
	list listResult
	// flag is set for classFlag.
	flag flag
	// year is set for classYear.
	year int
	// versions is set for classVersions.
	versions []versionDraft
}

// site mirrors rust/src/classify.rs Site.
type site struct {
	source    Source
	delimiter Delimiter
	pos       int
}

// classify mirrors rust/src/classify.rs classify. Stand-in: everything is Unknown.
func classify(inner string, s site, c *ctx) classification {
	_, _, _ = inner, s, c
	return classification{kind: classUnknown}
}

// junkKindOf mirrors rust/src/classify.rs junk_kind_of (Option<String> as (kind, ok)).
// Stand-in: nothing is junk.
func junkKindOf(words []string, c *ctx) (string, bool) {
	_, _ = words, c
	return "", false
}

// credit mirrors rust/src/credits.rs Credit.
type credit struct {
	name   string
	role   Role
	joiner *string
	source Source
	pos    int
	// list is the id of the credit list it came from (R7.6).
	list int
}

// positionedYear mirrors rust/src/credits.rs PositionedYear.
type positionedYear struct {
	year int
	pos  int
}

// listOptions mirrors rust/src/credits.rs ListOptions.
type listOptions struct {
	role            Role
	source          Source
	lead            *string
	splitWith       bool
	featMarkersJoin bool
}

// listResult mirrors rust/src/credits.rs ListResult.
type listResult struct {
	credits []credit
	unknown bool
	years   []positionedYear
}

// splitCredits mirrors rust/src/credits.rs split_credits. Stand-in: the whole list is one name.
func splitCredits(input skel, opts listOptions, c *ctx) listResult {
	name := collapseSpaces(renderSkel(input))
	if name == "" {
		return listResult{}
	}
	c.listSeq++
	return listResult{credits: []credit{{
		name: name, role: opts.role, joiner: opts.lead, source: opts.source,
		pos: startPos(input, 0), list: c.listSeq,
	}}}
}

// artistSideResult mirrors rust/src/artists.rs ArtistSideResult.
type artistSideResult struct {
	credits       []credit
	junk          []positionedJunk
	years         []positionedYear
	unknownArtist bool
	explicit      bool
	clean         bool
}

// parseArtistSide mirrors rust/src/artists.rs parse_artist_side. Stand-in: one primary credit.
func parseArtistSide(side skel, c *ctx) artistSideResult {
	r := splitCredits(side, listOptions{role: RolePrimary, source: SourceArtist, splitWith: true}, c)
	return artistSideResult{credits: r.credits}
}

// dedupArtists mirrors rust/src/assemble.rs dedup_artists. Stand-in: no dedup.
func dedupArtists(credits []credit) []Artist {
	out := make([]Artist, 0, len(credits))
	for _, x := range credits {
		out = append(out, Artist{Name: x.name, Role: x.role, Joiner: x.joiner, Source: x.source})
	}
	return out
}

// toVersion mirrors rust/src/assemble.rs to_version (RawVersion is the public Version in Go,
// whose Type is already a string).
func toVersion(v versionDraft) Version {
	return Version{
		Type: VersionType(v.vtype), Raw: v.raw, Artists: dedupArtists(v.artists),
		Modifiers: v.modifiers, Descriptor: v.descriptor, Year: v.year,
		UnknownArtist: v.unknownArtist, Delimiter: v.delimiter,
	}
}

// fullTitleOf mirrors rust/src/assemble.rs full_title_of.
func fullTitleOf(title string, versions []Version) string {
	_ = versions
	return title
}
