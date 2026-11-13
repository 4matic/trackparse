package trackparse

// R13.1 to R13.8: parseTracklist (js/src/tracklist/parse.ts).

// tlRow is one line on its way to an entry.
type tlRow struct {
	line tlLine
	scan timeScan
	// text is the R13.4.1 line text: rest without emphasis markers (R13.12) and without a
	// layered prefix (R13.13).
	text    string
	layered bool // R13.13
	blank   bool
	// candidate is R13.4.2: the line has a time or a spaced dash.
	candidate bool
	dash      bool
	probed    bool
	probe     lineProbe
	bare      *bareCandidate
	track     *Track
	// separated is R13.3: the track part has a separator.
	separated bool
	// marker is R13.3: the track part has a marker word.
	marker bool
}

// parseTracklist is R13.1 to R13.8 on one (already sanitized) text.
func parseTracklist(text string, o resolvedTracklist) Tracklist {
	markers := markerTableFor(o.markerWords)
	e := newEngine(o.parse)

	// R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
	// searches (after R2 and R4); the probe runs R1, R2 and R4 (R13.4.1).
	lines := splitLines(text)
	rows := make([]tlRow, len(lines))
	candidates, dashes := 0, 0
	for i, line := range lines {
		r := &rows[i]
		r.line = line
		r.scan = scanTime(line.raw)
		r.blank = isBlank(line.raw)
		scanned := []rune(r.scan.text)
		layered := 0
		if !r.blank {
			layered = layeredPrefix(scanned)
		}
		r.text = string(tail(scanned, layered))
		r.layered = layered > 0
		if r.blank {
			continue
		}
		r.probe = e.probe(r.text) // R13.4.1
		r.probed = true
		r.dash = r.probe.spacedDash
		r.candidate = r.scan.time != nil || r.dash
		// R13.5 / R13.13.3: a layered line never has a bare position.
		if !r.layered {
			r.bare = findBareCandidate(r.text, r.probe.position)
		}
		if r.candidate {
			candidates++
			if r.dash {
				dashes++
			}
		}
	}
	listContext := candidates > 0 && 2*dashes >= candidates

	// R13.5: confirmed bare positions are removed before parsing. R13.4.1: one parse per line.
	// R13.13.3: layered lines are not neighbours for the R13.5 search.
	bareNumbers := make([]int, len(rows))
	candidateLines := make([]bool, len(rows))
	for i := range rows {
		bareNumbers[i] = -1
		if rows[i].bare != nil {
			bareNumbers[i] = rows[i].bare.number
		}
		candidateLines[i] = rows[i].candidate && !rows[i].layered
	}
	confirmed := confirmPositions(bareNumbers, candidateLines)
	for i := range rows {
		r := &rows[i]
		if !r.probed {
			continue
		}
		var bare *bareCandidate
		if flagAt(confirmed, i) {
			bare = r.bare
		}
		// The probe already ran R1, R2 and R4; finish completes the parse from it. A line that
		// loses a bare position is probed again on the stripped text, so each probe is finished
		// exactly once.
		input, probe := r.text, &r.probe
		if bare != nil {
			input = bare.stripped
			stripped := e.probe(input)
			probe = &stripped
		}
		r.marker = hasMarkerWord(input, markers)
		// R13.4.2: the R6.10 tight dash only on timed lines without a marker word.
		track := probe.finish(listContext && r.scan.time != nil && !r.marker)
		if bare != nil {
			track.Position = &Position{Raw: bare.raw, Number: bare.number}
		}
		r.separated = hasSeparator(&track, probe.empty)
		r.track = &track
	}

	// R13.3
	entries := make([]Entry, len(rows))
	positions := make([]int, len(rows))
	invalid := make([]bool, len(rows))
	unparsed := make([]bool, len(rows))
	for i := range rows {
		r := &rows[i]
		kind := kindOf(r)
		var track *Track
		if kind == EntryKindTrack || kind == EntryKindMarker {
			track = r.track
		}
		positions[i] = -1
		if track != nil && track.Position != nil {
			positions[i] = track.Position.Number
		}
		invalid[i] = r.scan.invalid
		// R13.7 line-unparsed: a track entry whose track part has no separator (R13.3).
		unparsed[i] = kind == EntryKindTrack && !r.separated
		entries[i] = Entry{
			Line:    i + 1,
			Kind:    kind,
			Eol:     r.line.eol,
			Raw:     r.line.raw,
			Parts:   r.scan.parts,
			Time:    r.scan.time,
			Track:   track,
			Layered: r.layered,
		}
	}

	warnings := tracklistWarnings(&warningInput{
		entries:      entries,
		positions:    positions,
		invalid:      invalid,
		unparsed:     unparsed,
		audioSeconds: o.audioSeconds,
	})
	return Tracklist{
		SpecVersion: SpecVersion,
		Entries:     entries,
		Warnings:    warnings,
		Stats:       statsOf(entries),
	}
}

// kindOf is R13.3.
func kindOf(r *tlRow) EntryKind {
	if r.blank || r.track == nil {
		return EntryKindBlank
	}
	marker := !r.separated && r.marker
	switch {
	case r.scan.time != nil:
		if marker {
			return EntryKindMarker
		}
		return EntryKindTrack
	case isLink(r.text):
		return EntryKindLink
	case isHeader(r.text):
		return EntryKindHeader
	case marker:
		return EntryKindMarker
	case r.separated:
		return EntryKindTrack
	}
	return EntryKindNote
}

// statsOf counts the entries.
func statsOf(entries []Entry) Stats {
	s := Stats{Lines: len(entries)}
	for i := range entries {
		e := &entries[i]
		switch e.Kind {
		case EntryKindTrack:
			s.Tracks++
		case EntryKindMarker:
			s.Markers++
		}
		if e.Time != nil {
			s.Timed++
			seconds := e.Time.Seconds
			if s.FirstTime == nil {
				first := seconds
				s.FirstTime = &first
			}
			s.LastTime = &seconds
		}
	}
	return s
}
