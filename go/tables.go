package trackparse

import (
	"slices"
	"sync"
)

// Vocabulary tables compiled from spec/data (data_gen.go), optionally extended by
// options.keywords (R12). The default tables are built once and shared; tables with keywords
// are built per Parser. Tables are read-only after building, so they are safe for concurrent
// use. The maps in them are only looked up, never ranged over.

// spacedJoiner is one spaced joiner of joiners.json (R7.3).
type spacedJoiner struct {
	raw           string
	canonical     string
	isAnd         bool
	caseSensitive bool
}

// runInfo is the category of a word in the modifier run left of a version head (R5.7.2).
// Version types are strings: keywords.versionHeads values pass through to the output as given.
// A nil field is not set (JS undefined).
type runInfo struct {
	typeCapable *string
	descriptor  *string
	genre       bool
	head        *string
}

// tables are the vocabulary the pipeline reads.
type tables struct {
	featMarkers     *phraseTable[struct{}]
	bracketOnlyFeat *phraseTable[struct{}]
	producerMarkers *phraseTable[struct{}]
	// titleFeatMarkers are the only unbracketed feat markers on the title side, plus
	// keywords.featMarkers (R8.3).
	titleFeatMarkers *phraseTable[struct{}]
	// titleProducerMarkers are the only unbracketed producer markers on the title side (R8.3).
	titleProducerMarkers *phraseTable[struct{}]
	// junkOrGenre is junk phrases plus genres (kind genre): the R5.1 tokenizer vocabulary.
	junkOrGenre *phraseTable[string]
	// junkPhrases is junk phrases only (R8.4).
	junkPhrases      *phraseTable[string]
	junkConnectors   wordSet
	labelSuffixes    wordSet
	heads            *phraseTable[string]
	genericHeadTypes wordSet
	// runWords is everything allowed in the R5.7.2 modifier run, keyed by phrase.
	runWords      *phraseTable[runInfo]
	prefixForms   *phraseTable[string]
	spacedJoiners []spacedJoiner
	// tightJoiners maps a tight joiner to its canonical form.
	tightJoiners           map[string]string
	featCanonical          string
	noSplitBefore          wordSet
	stopwords              wordSet
	unknownCaseSensitive   wordSet
	unknownCaseInsensitive wordSet
	extensions             wordSet
	// platformSuffixes are sorted by UTF-16 length, longest first (stable).
	platformSuffixes []string
}

// lowerKeys is JS `asciiLower(s).trim()` of each entry, empty ones dropped.
func lowerKeys(list []string) []string {
	var out []string
	for _, s := range list {
		if k := jsTrim(asciiLower(s)); k != "" {
			out = append(out, k)
		}
	}
	return out
}

// dataEntries are the pairs of a data object in JS Object.entries order.
func dataEntries(list []dataKV) []kv {
	pairs := make([]kv, len(list))
	for i, p := range list {
		pairs[i] = kv(p)
	}
	return jsEntries(pairs)
}

// markerTable is a set-like phrase table of base plus extra.
func markerTable(base, extra []string) *phraseTable[struct{}] {
	t := newPhraseTable[struct{}]()
	for _, m := range base {
		t.add(m, struct{}{})
	}
	for _, m := range extra {
		t.add(m, struct{}{})
	}
	return t
}

// kindTable is a phrase table of string values.
func kindTable(entries ...[]kv) *phraseTable[string] {
	t := newPhraseTable[string]()
	for _, list := range entries {
		for _, e := range list {
			t.add(e.key, e.value)
		}
	}
	return t
}

// buildTables compiles the tables; kw nil means no extra words.
func buildTables(kw *keywordLists) *tables {
	if kw == nil {
		kw = &keywordLists{}
	}
	extraFeat := lowerKeys(kw.featMarkers)

	junkEntries := append(dataEntries(dataJunkPhrases), kw.junk...)
	genres := append(slices.Clone(dataGenresGenres), lowerKeys(kw.genres)...)
	genreEntries := make([]kv, len(genres))
	for i, g := range genres {
		genreEntries[i] = kv{g, string(JunkKindGenre)}
	}

	headEntries := append(dataEntries(dataVersionKeywordsHeads), kw.versionHeads...)

	// A Map in JS: the first note of a key fixes its place, later notes merge into it.
	var runKeys []string
	runIndex := map[string]int{}
	var runInfos []runInfo
	note := func(key string, patch func(*runInfo)) {
		k := jsTrim(asciiLower(key))
		i, ok := runIndex[k]
		if !ok {
			i = len(runInfos)
			runIndex[k] = i
			runKeys = append(runKeys, k)
			runInfos = append(runInfos, runInfo{})
		}
		patch(&runInfos[i])
	}
	for _, e := range dataEntries(dataVersionKeywordsTypeCapableModifiers) {
		t := e.value
		note(e.key, func(info *runInfo) { info.typeCapable = &t })
	}
	for _, d := range append(slices.Clone(dataDescriptorsDescriptors), lowerKeys(kw.descriptors)...) {
		value := jsTrim(asciiLower(d))
		note(d, func(info *runInfo) { info.descriptor = &value })
	}
	for _, g := range genres {
		note(g, func(info *runInfo) { info.genre = true })
	}
	for _, e := range headEntries {
		t := e.value
		note(e.key, func(info *runInfo) { info.head = &t })
	}
	runWords := newPhraseTable[runInfo]()
	for i, k := range runKeys {
		runWords.add(k, runInfos[i])
	}

	spaced := make([]spacedJoiner, len(dataJoinersSpaced))
	for i, j := range dataJoinersSpaced {
		spaced[i] = spacedJoiner{
			raw:           j.raw,
			canonical:     j.canonical,
			isAnd:         j.kind == "and",
			caseSensitive: j.caseSensitive,
		}
	}
	tight := make(map[string]string, len(dataJoinersTight))
	for _, j := range dataJoinersTight {
		tight[j.raw] = j.canonical
	}
	suffixes := slices.Clone(dataPlatformSuffixesSuffixes)
	// JS sorts by UTF-16 length, longest first; the sort is stable.
	slices.SortStableFunc(suffixes, func(a, b string) int {
		return utf16LenString(b) - utf16LenString(a)
	})

	return &tables{
		featMarkers:            markerTable(dataFeatMarkersMarkers, extraFeat),
		bracketOnlyFeat:        markerTable(dataFeatMarkersBracketOnly, nil),
		producerMarkers:        markerTable(dataFeatMarkersProducer, nil),
		titleFeatMarkers:       markerTable(dataFeatMarkersTitleMarkers, extraFeat),
		titleProducerMarkers:   markerTable(dataFeatMarkersTitleProducer, nil),
		junkOrGenre:            kindTable(junkEntries, genreEntries), // junk first: it wins over a genre
		junkPhrases:            kindTable(junkEntries),
		junkConnectors:         newWordSet(dataJunkConnectors),
		labelSuffixes:          newWordSet(dataJunkLabelSuffixes),
		heads:                  kindTable(headEntries),
		genericHeadTypes:       newWordSet(dataVersionKeywordsGenericHeads),
		runWords:               runWords,
		prefixForms:            kindTable(dataEntries(dataVersionKeywordsPrefixForms)),
		spacedJoiners:          spaced,
		tightJoiners:           tight,
		featCanonical:          dataJoinersFeatCanonical,
		noSplitBefore:          newWordSet(dataNoSplitBeforeWords),
		stopwords:              newWordSet(dataStopwordsWords),
		unknownCaseSensitive:   newWordSet(dataUnknownTokensCaseSensitive),
		unknownCaseInsensitive: newWordSet(dataUnknownTokensCaseInsensitive),
		extensions:             newWordSet(dataExtensionsExtensions),
		platformSuffixes:       suffixes,
	}
}

// defaultTables are the tables without keywords, built once.
var defaultTables = sync.OnceValue(func() *tables { return buildTables(nil) })

// tablesFor is the default tables when kw is nil, else fresh tables with the extra words.
func tablesFor(kw *keywordLists) *tables {
	if kw == nil {
		return defaultTables()
	}
	return buildTables(kw)
}

// isUnknownToken is R7.5 / R8.6: an unknown placeholder name (ID, ???, Untitled...).
func isUnknownToken(text string, t *tables) bool {
	return t.unknownCaseSensitive.has(text) || t.unknownCaseInsensitive.has(asciiLower(text))
}
