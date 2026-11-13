package trackparse

import "strings"

// R5: classification of a group's inner text (also used for dash suffixes and pipe segments).
// Port of js/src/classify.ts and rust/src/classify.rs.

// versionDraft is a version as R5 found it, before R9 orders and assembles it.
type versionDraft struct {
	// vtype is a version type name; keywords.versionHeads values pass through as given.
	vtype      VersionType
	raw        string
	artists    []credit
	modifiers  []string
	descriptor *string
	year       *int
	// unknownArtist is set when the credit was an unknown token (R7.5).
	unknownArtist bool
	delimiter     Delimiter
	pos           int
	// ambiguousMix is the R5.7.2 ambiguousMixCredit warning; emitted only when the version is
	// used.
	ambiguousMix bool
	// prefixForm is set when the R5.7.3 prefix form recognized it (R6.2 condition (ii) does not
	// apply).
	prefixForm bool
}

// flag is an R5.4 flag.
type flag uint8

const (
	flagExplicit flag = iota
	flagClean
)

// classKind is the kind of a classification.
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

// classification is the result of R5 (the Rust enum Classification). Only the fields of its
// kind are set.
type classification struct {
	kind classKind
	// junk is the kind of a classJunk.
	junk JunkKind
	// list is the credits of a classFeat or classProducer.
	list listResult
	// flag is the flag of a classFlag.
	flag flag
	// year is the year of a classYear.
	year int
	// versions are the versions of a classVersions.
	versions []versionDraft
}

// site is where the classified text sits: credits' source, versions' delimiter, absolute
// offset (UTF-16 units).
type site struct {
	source    Source
	delimiter Delimiter
	pos       int
}

var explicitFlags = []string{"explicit", "explicit version", "dirty", "dirty version"}
var cleanFlags = []string{"clean", "clean version", "radio clean"}

// classify is R5 on inner (already trimmed and whitespace-collapsed).
func classify(inner string, s site, c *ctx) classification {
	words := splitWords(inner)
	if len(words) == 0 {
		return classification{kind: classUnknown}
	}
	if kind, ok := junkKindOf(words, c); ok {
		return classification{kind: classJunk, junk: JunkKind(kind)}
	}
	if credit, ok := featOrProducer(inner, s, c); ok {
		return credit
	}
	if f, ok := flagOf(words); ok {
		return classification{kind: classFlag, flag: f}
	}
	if len(words) == 1 && isAllDigits(words[0]) {
		if year, ok := yearOf(words[0]); ok {
			return classification{kind: classYear, year: year}
		}
	}
	versions, ok := multiVersion(inner, s, c)
	if !ok {
		versions, ok = singleVersion(inner, s, c)
	}
	if ok {
		return classification{kind: classVersions, versions: versions}
	}
	return classification{kind: classUnknown}
}

// ---------------------------------------------------------------------------------------------
// R5.1 Junk

// junkKindOf is R5.1: the junk kind if the whole word list is junk vocabulary.
func junkKindOf(words []string, c *ctx) (string, bool) {
	t := c.t
	firstKind, found := "", false
	i := 0
	for i < len(words) {
		w := words[i]
		if m, ok := t.junkOrGenre.matchAt(words, i); ok {
			if !found {
				firstKind, found = m.value, true
			}
			i += m.length
			continue
		}
		// R5.1: connectors and year words may sit between junk phrases: Official Video 2015.
		if _, isYear := yearOf(w); t.junkConnectors.has(w) || t.junkConnectors.has(wordKey(w)) || isYear {
			i++
			continue
		}
		found = false
		break
	}
	// JS truthiness: an empty kind (possible through keywords.junk) does not count.
	if found && firstKind != "" {
		return firstKind, true
	}
	if len(words) >= 2 && inVocab(words[len(words)-1], t.labelSuffixes) {
		return string(JunkKindLabel), true
	}
	return "", false
}

// ---------------------------------------------------------------------------------------------
// R5.2 Feat, R5.3 Producer

func featOrProducer(inner string, s site, c *ctx) (classification, bool) {
	t := c.t
	text := []rune(inner)
	spans := wordSpans(text)
	words := spanTexts(spans)
	featLen := 0
	bracketOnly := false
	if m, ok := t.featMarkers.matchAt(words, 0); ok {
		featLen = m.length
	} else if m, ok := t.bracketOnlyFeat.matchAt(words, 0); ok {
		featLen, bracketOnly = m.length, true
	}
	if featLen > 0 && len(words) > featLen {
		stop := bracketOnly && featLen < len(words) && inVocab(words[featLen], t.stopwords)
		if !stop {
			r, ok := creditsAfter(text, spans, featLen, RoleFeatured, s, c)
			return classification{kind: classFeat, list: r}, ok
		}
	}
	prod, ok := t.producerMarkers.matchAt(words, 0)
	if !ok {
		return classification{}, false
	}
	n := prod.length
	if n < len(words) && wordKey(words[n]) == "by" {
		n++
	}
	if len(words) > n {
		r, ok := creditsAfter(text, spans, n, RoleProducer, s, c)
		return classification{kind: classProducer, list: r}, ok
	}
	return classification{}, false
}

func creditsAfter(inner []rune, spans []wordSpan, markerWords int, role Role, s site, c *ctx) (listResult, bool) {
	if len(spans) == 0 || markerWords < 1 || markerWords >= len(spans) {
		return listResult{}, false
	}
	first, lastMarker, firstName := spans[0], spans[markerWords-1], spans[markerWords]
	lead := string(sub(inner, first.start, lastMarker.end))
	// Offsets are UTF-16 units (see the scanner).
	base := s.pos + 1 + utf16Len(sub(inner, 0, firstName.start))
	opts := listOptions{
		role:            role,
		source:          s.source,
		lead:            &lead,
		splitWith:       false,
		featMarkersJoin: role == RoleFeatured,
	}
	return listFromText(tail(inner, firstName.start), base, c, &opts), true
}

// listFromText splits a plain string (nested brackets protected by a fresh scan) as a credit
// list.
func listFromText(text []rune, base int, c *ctx, opts *listOptions) listResult {
	s, _ := scan(text, base)
	return splitCredits(&s, opts, c)
}

// ---------------------------------------------------------------------------------------------
// R5.4 Flag

func flagOf(words []string) (flag, bool) {
	keys := make([]string, len(words))
	for i, w := range words {
		keys[i] = wordKey(w)
	}
	key := strings.Join(keys, " ")
	for _, f := range explicitFlags {
		if key == f {
			return flagExplicit, true
		}
	}
	for _, f := range cleanFlags {
		if key == f {
			return flagClean, true
		}
	}
	return 0, false
}

// ---------------------------------------------------------------------------------------------
// R5.6 Multi-version

// splitMultiParts splits at top-level " / " or "; " (outside nested brackets).
func splitMultiParts(inner []rune) []string {
	var parts [][]rune
	depth := 0
	start := 0
	for i, ch := range inner {
		switch {
		case ch == '(' || ch == '[' || ch == '{':
			depth++
		case (ch == ')' || ch == ']' || ch == '}') && depth > 0:
			depth--
		case depth == 0 && ch == '/' && before(inner, i) == ' ' && at(inner, i+1) == ' ':
			parts = append(parts, sub(inner, start, i-1))
			start = i + 2
		case depth == 0 && ch == ';' && at(inner, i+1) == ' ':
			parts = append(parts, sub(inner, start, i))
			start = i + 2
		}
	}
	parts = append(parts, tail(inner, start))
	out := make([]string, len(parts))
	for i, p := range parts {
		out[i] = jsTrim(string(p))
	}
	return out
}

func multiVersion(inner string, s site, c *ctx) ([]versionDraft, bool) {
	parts := splitMultiParts([]rune(inner))
	if len(parts) < 2 {
		return nil, false
	}
	var out []versionDraft
	for _, part := range parts {
		if part == "" {
			return nil, false
		}
		vs, ok := singleVersion(part, s, c)
		if !ok {
			return nil, false
		}
		out = append(out, vs...)
	}
	return out, true
}

// ---------------------------------------------------------------------------------------------
// R5.7 Version

func singleVersion(raw string, s site, c *ctx) ([]versionDraft, bool) {
	text := []rune(raw)
	spans := wordSpans(text)
	words := spanTexts(spans)
	if v, ok := headBy(raw, text, spans, words, s, c); ok {
		return []versionDraft{v}, true
	}
	if v, ok := headScan(raw, words, s, c); ok {
		return []versionDraft{v}, true
	}
	if v, ok := prefixForm(raw, words, s, c); ok {
		return []versionDraft{v}, true
	}
	return nil, false
}

func baseVersion(raw string, vtype string, s site) versionDraft {
	return versionDraft{
		vtype:     VersionType(vtype),
		raw:       raw,
		artists:   []credit{},
		modifiers: []string{},
		delimiter: s.delimiter,
		pos:       s.pos,
	}
}

func remixerOpts() listOptions {
	return listOptions{role: RoleRemixer, source: SourceVersion}
}

// dedupRemixers is R9.2 for remixers: deduped within their own version only.
func dedupRemixers(credits []credit) []credit {
	seen := map[string]struct{}{}
	out := make([]credit, 0, len(credits))
	for _, cr := range credits {
		key := dedupKey(cr.name)
		if _, dup := seen[key]; dup {
			continue
		}
		seen[key] = struct{}{}
		out = append(out, cr)
	}
	return out
}

func setCredits(v *versionDraft, text []rune, s site, c *ctx) {
	opts := remixerOpts()
	r := listFromText(text, s.pos, c, &opts)
	v.artists = dedupRemixers(r.credits)
	if r.unknown {
		v.unknownArtist = true
	}
	// R7.5: a dropped year-only name sets the version's year if it is still null.
	if len(r.years) > 0 && v.year == nil {
		y := r.years[0].year
		v.year = &y
	}
}

// headBy is R5.7.1: Remix by Skrillex.
func headBy(raw string, text []rune, spans []wordSpan, words []string, s site, c *ctx) (versionDraft, bool) {
	head, ok := c.t.heads.matchAt(words, 0)
	if !ok || head.length+1 >= len(spans) || head.length >= len(words) {
		return versionDraft{}, false
	}
	by, rest := words[head.length], spans[head.length+1]
	if wordKey(by) != "by" {
		return versionDraft{}, false
	}
	v := baseVersion(raw, head.value, s)
	setCredits(&v, tail(text, rest.start), s, c)
	return v, true
}

// runItem is one entry of the R5.7.2 modifier run: a run word (info) or a year.
type runItem struct {
	start int
	end   int
	info  runInfo
	year  *int
}

// collectRun walks left from end collecting the modifier run (R5.7.2).
func collectRun(words []string, end int, c *ctx) []runItem {
	var items []runItem
	j := end
	for j > 0 {
		if m, ok := c.t.runWords.matchEnding(words, j); ok {
			items = append(items, runItem{start: j - m.length, end: j, info: m.value})
			j -= m.length
			continue
		}
		year, ok := yearOf(words[j-1])
		if !ok {
			break
		}
		items = append(items, runItem{start: j - 1, end: j, year: &year})
		j--
	}
	for a, b := 0, len(items)-1; a < b; a, b = a+1, b-1 {
		items[a], items[b] = items[b], items[a]
	}
	return items
}

// truthyName is a JS truthiness test on an optional type name: nil and "" do not count.
func truthyName(s *string) (string, bool) {
	if s == nil || *s == "" {
		return "", false
	}
	return *s, true
}

// headScan is R5.7.2: Extended VIP Mix, Skrillex Remix, Taylor's Version.
func headScan(raw string, words []string, s site, c *ctx) (versionDraft, bool) {
	t := c.t
	head, ok := t.heads.matchEnding(words, len(words))
	if !ok {
		return versionDraft{}, false
	}
	headType := head.value
	headStart := len(words) - head.length
	run := collectRun(words, headStart, c)
	creditEnd := headStart
	if len(run) > 0 {
		creditEnd = run[0].start
	}
	creditWords := words[:creditEnd]
	left := len(run) - 1 // -1: no run

	// R5.7.2 type resolution. Generic = canonical type mix/edit/version, any spelling.
	generic := t.genericHeadTypes.has(headType)
	vtype := headType
	typeItem := -1
	viaMix := false
	nearestHead := -1
	if generic {
		nearestHead = nearestNonGenericHead(run, c)
	}
	var leftCapable, nearestType string
	leftOK, nearestOK := false, false
	if left >= 0 {
		leftCapable, leftOK = truthyName(run[left].info.typeCapable)
	}
	if nearestHead >= 0 {
		nearestType, nearestOK = truthyName(run[nearestHead].info.head)
	}
	switch {
	case generic && leftOK:
		vtype = leftCapable
		typeItem = left
	case nearestOK:
		// 2011 Remastered Version: remaster
		vtype = nearestType
		typeItem = nearestHead
	case headType == "mix" && len(creditWords) > 0:
		vtype = "remix"
		viaMix = true
	}
	v := baseVersion(raw, vtype, s)

	var descriptors []string
	for i, item := range run {
		if item.year != nil {
			if v.year == nil {
				y := *item.year
				v.year = &y
			}
			continue
		}
		modifier := item.info.typeCapable
		if modifier == nil {
			modifier = item.info.descriptor
		}
		if modifier != nil {
			if i != typeItem && !containsString(v.modifiers, *modifier) {
				v.modifiers = append(v.modifiers, *modifier)
			}
		} else if item.info.genre {
			descriptors = append(descriptors, strings.Join(words[item.start:item.end], " "))
		}
	}

	if len(creditWords) > 0 {
		credit := strings.Join(creditWords, " ")
		lastWord := asciiLower(creditWords[len(creditWords)-1])
		switch {
		case isUnknownToken(credit, t):
			v.unknownArtist = true
		case vtype == "version" || vtype == "cover" || isAllJunkPhrases(creditWords, c):
			// R5.7.2: Japanese Version, Taylor's Version, Adele Cover, Video Edit.
			descriptors = append([]string{credit}, descriptors...)
		default:
			name := []rune(credit)
			if strings.HasSuffix(lastWord, "'s") {
				name = name[:max(len(name)-2, 0)]
			}
			setCredits(&v, name, s, c)
			if n := len(v.artists); viaMix && n > 0 && len(splitWords(v.artists[n-1].name)) >= 3 {
				v.ambiguousMix = true
			}
		}
	}
	if len(descriptors) > 0 {
		d := strings.Join(descriptors, " ")
		v.descriptor = &d
	}
	return v, true
}

// containsString reports whether list holds s.
func containsString(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

// nearestNonGenericHead is R5.7.2 type rule 2: the index of the non-generic head in the run
// nearest the (generic) head, or -1.
func nearestNonGenericHead(run []runItem, c *ctx) int {
	for i := len(run) - 1; i >= 0; i-- {
		if h := run[i].info.head; h != nil && !c.t.genericHeadTypes.has(*h) {
			return i
		}
	}
	return -1
}

// isAllJunkPhrases is R5.7.2: the credit span is made entirely of junk phrases (Video in Video
// Edit).
func isAllJunkPhrases(words []string, c *ctx) bool {
	i := 0
	for i < len(words) {
		m, ok := c.t.junkPhrases.matchAt(words, i)
		if !ok {
			return false
		}
		i += m.length
	}
	return len(words) > 0
}

// prefixForm is R5.7.3: Live at Wembley 1986, Remastered 2015, Sped Up.
func prefixForm(raw string, words []string, s site, c *ctx) (versionDraft, bool) {
	m, ok := c.t.prefixForms.matchAt(words, 0)
	if !ok {
		return versionDraft{}, false
	}
	v := baseVersion(raw, m.value, s)
	v.prefixForm = true
	var rest []string
	for _, w := range tailWords(words, m.length) {
		if v.year == nil {
			if year, ok := yearOf(w); ok {
				v.year = &year
				continue
			}
		}
		rest = append(rest, w)
	}
	if len(rest) > 0 {
		d := strings.Join(rest, " ")
		v.descriptor = &d
	}
	return v, true
}

// tailWords is words[from:], or nil when from is past the end.
func tailWords(words []string, from int) []string {
	if from < 0 || from >= len(words) {
		return nil
	}
	return words[from:]
}
