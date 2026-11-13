package trackparse

import (
	"cmp"
	"slices"
	"strings"
)

// The pipeline: R1, R2, R4, R3/R6, R7, R8, R9, plus R10 runArtists (js/src/parser.ts,
// rust/src/parser.rs). The engine itself (options, tables, known keys) is in context.go.

// uploaderBase is the offset used for uploader text, so it sorts before everything from the
// input.
const uploaderBase = -1_000_000_000

// skelOf scans a piece and trims it, warning on unbalanced brackets (R3.3).
func skelOf(p piece, c *ctx) skel {
	sk, unbalanced := scan(p.text, p.base)
	if unbalanced {
		c.warn(WarningUnbalancedBrackets)
	}
	return trimSkel(sk)
}

// uploaderSkel is R6.8: the uploader minus a trailing " - Topic" and "VEVO", or nil.
func uploaderSkel(uploader string) *skel {
	if uploader == "" {
		return nil
	}
	u := normalize(uploader)
	if rest, ok := strings.CutSuffix(u, " - Topic"); ok {
		u = rest
	}
	if rest, ok := strings.CutSuffix(u, "VEVO"); ok {
		u = jsTrimEnd(rest)
	}
	if u == "" {
		return nil
	}
	sk, _ := scan([]rune(u), uploaderBase)
	return &sk
}

// emptyTrack is the R9.5 result for an input that is empty after R1, and the start of every
// other result.
func emptyTrack(input string, mode Mode) Track {
	return Track{
		Input:    input,
		Mode:     mode,
		Artists:  []Artist{},
		Versions: []Version{},
		Junk:     []Junk{},
		Warnings: []Warning{},
	}
}

// staged is the R1, R2 and R4 stage of run, kept so the tracklist probe's work is not redone.
type staged struct {
	e     *engine
	c     *ctx
	pre   preludeResult
	track Track
	// working is the skeleton after R4; nil when the input is empty after R1 (R9.5: track is
	// final).
	working *skel
}

// stage runs R1, R2 and R4.
func (e *engine) stage(input string) staged {
	c := e.newCtx()
	norm := normalize(input)
	pre := prelude(norm, e.opts.mode, c)
	track := emptyTrack(input, pre.mode)
	if norm == "" {
		return staged{e: e, c: c, pre: pre, track: track} // R9.5
	}

	// R4
	prefix := stripPrefixes(pre.working.text, pre.working.base, pre.mode, c)
	track.Timestamp = prefix.timestamp
	track.Position = prefix.position
	working := skelOf(piece{text: prefix.rest, base: prefix.base}, c)
	return staged{e: e, c: c, pre: pre, track: track, working: &working}
}

// run is R1 to R9 on one (already sanitized) input; listContext is the R13.4.2 flag that only
// matters in tracklist mode (R6.10).
func (e *engine) run(input string, listContext bool) Track {
	return finish(e.stage(input), listContext)
}

// runArtists is R10 on one (already sanitized) input.
func (e *engine) runArtists(input string) []Artist {
	c := e.newCtx()
	norm := normalize(input)
	if norm == "" {
		return []Artist{}
	}
	sk, _ := scan([]rune(norm), 0)
	side := parseArtistSide(sk, c)
	artists := dedupArtists(side.credits)
	for i := range artists {
		artists[i].Source = SourceArtist
	}
	if artists == nil {
		artists = []Artist{}
	}
	return artists
}

// lineProbe is facts about a line's prefix and separator search, for the tracklist module
// (R13.4.2, R13.5).
type lineProbe struct {
	// position: R4.2 took a position.
	position bool
	// spacedDash: the skeleton R6 searches (after R2 and R4) has a spaced dash (R6.1).
	spacedDash bool
	// empty: the input is empty after R1.
	empty  bool
	staged staged
}

// probe is R1, R2 and R4 only, as run does them: no artist or title parsing. finish completes
// the parse from the same stage, so a probed line is not normalized twice.
func (e *engine) probe(input string) lineProbe {
	st := e.stage(input)
	return lineProbe{
		position: st.track.Position != nil,
		spacedDash: st.pre.pipeTitle == nil && st.working != nil &&
			len(splitAtSpacedDashes(*st.working, true)) >= 2,
		empty:  st.working == nil,
		staged: st,
	}
}

// finish is the parse of the probed input (R6 to R9 only). Call it once per probe: the probe's
// per-call state (warnings) is consumed.
func (p *lineProbe) finish(listContext bool) Track {
	return finish(p.staged, listContext)
}

// byPos sorts items by offset, stably (like the JS sort).
func byPos[T any](items []T, pos func(*T) int) []T {
	slices.SortStableFunc(items, func(a, b T) int { return cmp.Compare(pos(&a), pos(&b)) })
	return items
}

// finish is R6 to R9 on a staged input.
func finish(st staged, listContext bool) Track {
	c, pre, track := st.c, st.pre, st.track
	if st.working == nil {
		return track
	}
	working := *st.working

	var out extracted
	var artistSkel *skel
	var titleSkel skel
	var relaxed bool
	if pre.pipeTitle != nil {
		// R2.4 step 3: "artist | title", R6 skipped.
		titleSkel = skelOf(*pre.pipeTitle, c)
		if !working.isEmpty() {
			artistSkel = &working
		}
		relaxed = artistSkel != nil
	} else {
		sep := findSeparator(separatorInput{
			skel:        working,
			mode:        pre.mode,
			uploader:    uploaderSkel(st.e.opts.uploader),
			listContext: listContext,
		}, c)
		addPeeled(&out, sep.peeled)
		artistSkel = sep.artist
		titleSkel = sep.title
		relaxed = sep.split
	}

	var artistSide artistSideResult
	if artistSkel != nil {
		artistSide = parseArtistSide(*artistSkel, c)
	}
	titleSide := parseTitleSide(titleSkel, relaxed, pre.mode == ModeYouTube, &out, c)

	// R9
	versions := byPos(out.versions, func(v *versionDraft) int { return v.pos })
	for _, v := range versions {
		if v.ambiguousMix {
			c.warn(WarningAmbiguousMixCredit)
			break
		}
	}
	credits := slices.Concat(artistSide.credits, byPos(out.credits, func(x *credit) int { return x.pos }))
	track.Artists = dedupArtists(credits)
	if track.Artists == nil {
		track.Artists = []Artist{}
	}
	track.Title = titleSide.title
	track.Versions = make([]Version, 0, len(versions))
	for _, v := range versions {
		track.Versions = append(track.Versions, toVersion(v))
	}
	track.FullTitle = fullTitleOf(track.Title, track.Versions)
	// R9.6: the first year in text order.
	years := slices.Concat(artistSide.years, out.years)
	years = byPos(years, func(y *positionedYear) int { return y.pos })
	if len(years) > 0 {
		y := years[0].year
		track.Year = &y
	}
	track.Flags = Flags{
		Explicit:      out.explicit || artistSide.explicit,
		Clean:         out.clean || artistSide.clean,
		UnknownArtist: artistSide.unknownArtist || out.unknownArtist,
		UnknownTitle:  titleSide.unknownTitle,
	}
	junk := slices.Concat(pre.junk, artistSide.junk, out.junk)
	junk = byPos(junk, func(j *positionedJunk) int { return j.pos })
	track.Junk = make([]Junk, 0, len(junk))
	for _, j := range junk {
		track.Junk = append(track.Junk, Junk{Raw: j.raw, Kind: JunkKind(j.kind)})
	}
	track.Warnings = append([]Warning{}, c.warnings...)
	return track
}
