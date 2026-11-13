package trackparse

import "strings"

// R11: render a parsed track back to a string. Port of js/src/format.ts and rust/src/format.rs.
// R11 always reads the default tables: keyword options are parse options and do not reach it.

// isFeatMarker reports whether raw, as one word, is a feat marker.
func isFeatMarker(raw string, t *tables) bool {
	w := []string{raw}
	if _, ok := t.featMarkers.matchAt(w, 0); ok {
		return true
	}
	_, ok := t.bracketOnlyFeat.matchAt(w, 0)
	return ok
}

// canonicalJoiner is R11.5: map a raw joiner to its canonical form through data/joiners.json;
// feat markers become featCanonical. A featured list's lead marker is always a feat marker
// (with becomes feat.), while between names the joiner table wins (Sigala with Ella Eyre keeps
// with).
func canonicalJoiner(raw string, t *tables, featLead bool) string {
	if featLead && isFeatMarker(raw, t) {
		return t.featCanonical
	}
	if tight, ok := t.tightJoiners[raw]; ok {
		return tight
	}
	for _, j := range t.spacedJoiners {
		matches := raw == j.raw
		if !j.caseSensitive {
			matches = wordMatches(raw, j.raw)
		}
		if matches {
			return j.canonical
		}
	}
	if isFeatMarker(raw, t) {
		return t.featCanonical
	}
	return raw
}

// joinSep is R11.1: " " + joiner + " "; tight joiners as the raw character + " "; a missing
// joiner as &.
func joinSep(joiner *string, o *resolvedFormat, t *tables) string {
	j := "&"
	if joiner != nil {
		j = *joiner
		if o.canonical {
			j = canonicalJoiner(j, t, false)
		}
	}
	if _, tight := t.tightJoiners[j]; tight {
		return j + " "
	}
	return " " + j + " "
}

// joinNames is the names of one credit list joined by their own joiners (the first joiner is
// not rendered).
func joinNames(list []*Artist, o *resolvedFormat, t *tables) string {
	var b strings.Builder
	for i, a := range list {
		if i > 0 {
			b.WriteString(joinSep(a.Joiner, o, t))
		}
		b.WriteString(a.Name)
	}
	return b.String()
}

// leadMarker is the marker in front of a featured list.
func leadMarker(first *Artist, o *resolvedFormat, t *tables, fallback string) string {
	if o.featMarker != nil {
		return *o.featMarker
	}
	raw := fallback
	if first.Joiner != nil {
		raw = *first.Joiner
	}
	if o.canonical {
		return canonicalJoiner(raw, t, true)
	}
	return raw
}

// withRole is the artists of track with role, in order.
func withRole(artists []Artist, role Role) []*Artist {
	var out []*Artist
	for i := range artists {
		if artists[i].Role == role {
			out = append(out, &artists[i])
		}
	}
	return out
}

// formatTrack is R11: t rendered back to a string with the resolved format options.
func formatTrack(track *Track, o resolvedFormat) string {
	t := defaultTables()
	primaries := withRole(track.Artists, RolePrimary)
	featured := withRole(track.Artists, RoleFeatured)
	producers := withRole(track.Artists, RoleProducer)

	var artistFeat, titleFeat []*Artist
	switch o.feat {
	case featSource:
		for _, a := range featured {
			if a.Source == SourceArtist {
				artistFeat = append(artistFeat, a)
			} else {
				titleFeat = append(titleFeat, a)
			}
		}
	case featArtist:
		artistFeat = featured
	case featTitle:
		titleFeat = featured
	}

	// R11.1 / R11.2
	artistPart := joinNames(primaries, &o, t)
	if len(artistFeat) > 0 {
		lead := leadMarker(artistFeat[0], &o, t, t.featCanonical)
		names := joinNames(artistFeat, &o, t)
		if artistPart == "" {
			artistPart = lead + " " + names
		} else {
			artistPart = artistPart + " " + lead + " " + names
		}
	}

	// R11.3
	var pieces []string
	if track.Title != "" {
		pieces = append(pieces, track.Title)
	}
	if len(titleFeat) > 0 {
		lead := leadMarker(titleFeat[0], &o, t, t.featCanonical)
		pieces = append(pieces, "("+lead+" "+joinNames(titleFeat, &o, t)+")")
	}
	if o.versions {
		for _, v := range track.Versions {
			pieces = append(pieces, renderVersion(v.Raw, v.Delimiter))
		}
	}
	if len(producers) > 0 && o.producers {
		marker := "prod."
		if producers[0].Joiner != nil {
			marker = *producers[0].Joiner
		}
		pieces = append(pieces, "("+marker+" "+joinNames(producers, &o, t)+")")
	}
	titlePart := strings.Join(pieces, " ")

	// R11.4
	result := titlePart
	if artistPart != "" {
		result = artistPart + " - " + titlePart
	}
	if o.position && track.Position != nil {
		result = track.Position.Raw + ". " + result
	}
	return result
}

// allArtists is every credited name: the track's artists plus every version's remixers,
// deduplicated by the R0.4 key, first occurrence kept.
func allArtists(track *Track) []Artist {
	seen := map[string]struct{}{}
	out := []Artist{}
	add := func(a Artist) {
		key := dedupKey(a.Name)
		if _, dup := seen[key]; dup {
			return
		}
		seen[key] = struct{}{}
		out = append(out, a)
	}
	for _, a := range track.Artists {
		add(a)
	}
	for _, v := range track.Versions {
		for _, a := range v.Artists {
			add(a)
		}
	}
	return out
}
