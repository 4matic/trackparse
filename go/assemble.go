package trackparse

import "strings"

// R9: assembly and dedup. Port of js/src/assemble.ts and rust/src/assemble.rs.

// precedence is the R9.2 role order: a later duplicate with a higher one upgrades the kept
// entry.
func precedence(role Role) int {
	switch role {
	case RolePrimary:
		return 2
	case RoleFeatured:
		return 1
	}
	return 0
}

// toArtist is the public form of a credit.
func toArtist(c credit) Artist {
	return Artist{Name: c.name, Role: c.role, Joiner: c.joiner, Source: c.source}
}

// dedupEntry is one credit during R9.2.
type dedupEntry struct {
	a     Artist
	list  int
	alive bool
}

// creditLists holds the remaining names of each credit list, so "the list's first remaining
// name" and "the next one" are cheap and dedup stays near-linear (O(n log n)) on inputs with
// thousands of duplicate credits. The JS reference scans the whole list for each duplicate,
// which is quadratic; the output is the same.
//
// Each list is a min-heap of entry indexes with lazy deletion: an index on a heap is a member
// only while its entry is alive and still belongs to that list. An index can sit on a heap more
// than once (an entry that left a list and came back, which R9.2 role upgrades allow); copies
// are popped together.
type creditLists struct {
	entries []dedupEntry
	heaps   map[int][]int
}

// newCreditLists indexes the lists of entries.
func newCreditLists(entries []dedupEntry) *creditLists {
	l := &creditLists{entries: entries, heaps: map[int][]int{}}
	for i := range entries {
		l.insert(entries[i].list, i)
	}
	return l
}

// member reports whether entry x is a remaining name of list.
func (l *creditLists) member(list, x int) bool {
	return x >= 0 && x < len(l.entries) && l.entries[x].alive && l.entries[x].list == list
}

// insert puts entry e on the heap of list.
func (l *creditLists) insert(list, e int) {
	h := append(l.heaps[list], e)
	// Sift up.
	for i := len(h) - 1; i > 0; {
		p := (i - 1) / 2
		if h[p] <= h[i] {
			break
		}
		h[p], h[i] = h[i], h[p]
		i = p
	}
	l.heaps[list] = h
}

// pop removes the smallest index of list's heap.
func (l *creditLists) pop(list int) {
	h := l.heaps[list]
	n := len(h)
	if n == 0 {
		return
	}
	h[0] = h[n-1]
	h = h[:n-1]
	// Sift down.
	for i := 0; ; {
		a, b, m := 2*i+1, 2*i+2, i
		if a < len(h) && h[a] < h[m] {
			m = a
		}
		if b < len(h) && h[b] < h[m] {
			m = b
		}
		if m == i {
			break
		}
		h[m], h[i] = h[i], h[m]
		i = m
	}
	l.heaps[list] = h
}

// first is the smallest remaining index of list after dropping stale heap entries, or -1.
func (l *creditLists) first(list int) int {
	for {
		h := l.heaps[list]
		if len(h) == 0 {
			return -1
		}
		if l.member(list, h[0]) {
			return h[0]
		}
		l.pop(list)
	}
}

// heir is the entry that follows e in list when e is the list's first remaining name, else -1.
// It must only be called when e is about to leave list: it takes e off the heap.
func (l *creditLists) heir(list, e int) int {
	if l.first(list) != e {
		return -1
	}
	for {
		h := l.heaps[list]
		if len(h) == 0 {
			return -1
		}
		if x := h[0]; x != e && l.member(list, x) {
			return x
		}
		l.pop(list)
	}
}

// passJoiner is R7.6: entry e leaves its list. If it was the list's first remaining name, the
// next remaining name of that list takes joiner.
func (l *creditLists) passJoiner(e int, joiner *string) {
	if e < 0 || e >= len(l.entries) {
		return
	}
	if next := l.heir(l.entries[e].list, e); next >= 0 {
		l.entries[next].a.Joiner = joiner
	}
}

// dedupKeyOf is the R9.2 key: producers are a separate credit class, deduped only among
// themselves.
func dedupKeyOf(a *Artist) string {
	class := "c:"
	if a.Role == RoleProducer {
		class = "p:"
	}
	return class + dedupKey(a.Name)
}

// dedupArtists is R9.2: keep the first occurrence. A later lower/equal-precedence duplicate is
// removed (R7.6 joiner inheritance); a later higher-precedence duplicate (role upgrade) gives
// the kept entry its role, joiner and source, and the kept entry moves into that occurrence's
// list.
func dedupArtists(credits []credit) []Artist {
	entries := make([]dedupEntry, len(credits))
	for i, c := range credits {
		entries[i] = dedupEntry{a: toArtist(c), list: c.list, alive: true}
	}
	lists := newCreditLists(entries)
	index := map[string]int{}
	for i := range entries {
		e := &entries[i]
		key := dedupKeyOf(&e.a)
		k, dup := index[key]
		if !dup {
			index[key] = i
			continue
		}
		kept := &entries[k]
		if precedence(e.a.Role) > precedence(kept.a.Role) {
			lists.passJoiner(k, kept.a.Joiner)
			kept.a.Role = e.a.Role
			kept.a.Joiner = e.a.Joiner
			kept.a.Source = e.a.Source
			kept.list = e.list
			lists.insert(e.list, k)
		} else {
			lists.passJoiner(i, e.a.Joiner)
		}
		e.alive = false
	}
	out := make([]Artist, 0, len(entries))
	for _, e := range entries {
		if e.alive {
			out = append(out, e.a)
		}
	}
	return out
}

// toVersion is the public form of a version draft. Version types are typed strings, so a type
// added through keywords.versionHeads reaches the output as given.
func toVersion(v versionDraft) Version {
	artists := make([]Artist, len(v.artists))
	for i, c := range v.artists {
		artists[i] = toArtist(c)
	}
	modifiers := make([]string, len(v.modifiers))
	copy(modifiers, v.modifiers)
	return Version{
		Type:          v.vtype,
		Raw:           v.raw,
		Artists:       artists,
		Modifiers:     modifiers,
		Descriptor:    v.descriptor,
		Year:          v.year,
		UnknownArtist: v.unknownArtist,
		Delimiter:     v.delimiter,
	}
}

// renderVersion is R9.4: a version rendered with its delimiter.
func renderVersion(raw string, delimiter Delimiter) string {
	switch delimiter {
	case DelimiterParen:
		return "(" + raw + ")"
	case DelimiterSquare:
		return "[" + raw + "]"
	case DelimiterCurly:
		return "{" + raw + "}"
	}
	return "- " + raw
}

// fullTitleOf is R9.4: the title followed by its versions as written, empty pieces skipped.
func fullTitleOf(title string, versions []Version) string {
	pieces := make([]string, 0, len(versions)+1)
	if title != "" {
		pieces = append(pieces, title)
	}
	for _, v := range versions {
		if s := renderVersion(v.Raw, v.Delimiter); s != "" {
			pieces = append(pieces, s)
		}
	}
	return strings.Join(pieces, " ")
}
