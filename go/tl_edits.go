package trackparse

import (
	"cmp"
	"slices"
	"strings"
)

// R13.9: applyEdits. Edits apply in order, each to the result of the previous one.
//
// Edits are read as loose JSON objects with the number rules of the JS reference (a bool is not
// a number; null, NaN and infinities are invalid). Typed Edit values are written to that form
// first (ApplyEdits), so both doors share one code path.

// workLine is one line of the text being edited.
type workLine struct {
	raw string
	eol Eol
	// changed is true when an edit wrote the line (R13.9.1).
	changed bool
	// scan is scanTime(raw), kept in step with raw (each line is scanned once per write).
	scan timeScan
}

// editCtx is the edit state. Every edit validates its input before it mutates anything, so an
// invalid edit (R13.9.6) only has to drop the warnings it added: no snapshot of the lines is
// needed.
type editCtx struct {
	lines    []*workLine
	index    int
	warnings []rawEditWarning
	// oneDigit and wideHours count the times with one-digit hours and with wider hours, over
	// all lines (R13.9.2).
	oneDigit  int
	wideHours int
	// eols counts the lines ending with LF, CRLF and CR (R13.9.5).
	eols [3]int
}

func newWorkLine(raw string, eol Eol, changed bool) *workLine {
	return &workLine{raw: raw, eol: eol, changed: changed, scan: scanTime(raw)}
}

// eolSlot is the index of eol in editCtx.eols, -1 for none.
func eolSlot(eol Eol) int {
	switch eol {
	case EolLF:
		return 0
	case EolCRLF:
		return 1
	case EolCR:
		return 2
	}
	return -1
}

// countHours adds (by = 1) or removes (by = -1) a time from the hour-width counts (R13.9.2).
func (c *editCtx) countHours(s *timeScan, by int) {
	if s.style == nil || !s.style.hours {
		return
	}
	if s.style.hourWidth == 1 {
		c.oneDigit += by
	} else {
		c.wideHours += by
	}
}

func (c *editCtx) countEol(eol Eol, by int) {
	if k := eolSlot(eol); k >= 0 {
		c.eols[k] += by
	}
}

// countLine is a line entering (by = 1) or leaving (by = -1) the text.
func (c *editCtx) countLine(l *workLine, by int) {
	c.countHours(&l.scan, by)
	c.countEol(l.eol, by)
}

func (c *editCtx) setEol(l *workLine, eol Eol) {
	c.countEol(l.eol, -1)
	l.eol = eol
	l.changed = true
	c.countEol(eol, 1)
}

func (c *editCtx) setRaw(l *workLine, raw string) {
	c.countHours(&l.scan, -1)
	l.raw = raw
	l.scan = scanTime(raw)
	l.changed = true
	c.countHours(&l.scan, 1)
}

func (c *editCtx) warn(code EditWarningCode, line *float64) {
	c.warnings = append(c.warnings, rawEditWarning{code: code, edit: c.index, line: line})
}

// lineIndex is the 0-based index of an existing 1-based line number.
func (c *editCtx) lineIndex(v jsonValue, present bool) (int, bool) {
	n, ok := jsInteger(v, present)
	if !ok || n < 1 || n > float64(len(c.lines)) {
		return 0, false
	}
	return int(n) - 1, true
}

// oneDigitHours is R13.9.2: do the text's times with hours all use one-digit hours (and is
// there at least one)?
func (c *editCtx) oneDigitHours() bool {
	return c.oneDigit > 0 && c.wideHours == 0
}

// singleLine is a string without a line break.
func singleLine(v jsonValue, present bool) (string, bool) {
	if !present || v.kind != jsonString || hasLineBreak(v.str) {
		return "", false
	}
	return v.str, true
}

// retimed is a line rewritten for a new time.
type retimed struct {
	index    int
	raw      string
	clamped  bool
	upgraded bool
}

// retime is R13.9.2 / R13.9.3: the rewritten line for a new value; false when it cannot be
// written.
func (c *editCtx) retime(index int, value float64, oneDigit bool) (retimed, bool) {
	if index < 0 || index >= len(c.lines) {
		return retimed{}, false
	}
	s := &c.lines[index].scan
	if s.style == nil || s.time == nil {
		return retimed{}, false
	}
	clamped := value < 0
	if clamped {
		value = 0
	}
	f, ok := formatTime(value, s.style, oneDigit)
	if !ok {
		return retimed{}, false
	}
	p := &s.parts
	return retimed{index: index, raw: p.Lead + f.raw + p.Gap + p.Rest, clamped: clamped, upgraded: f.upgraded}, true
}

func (c *editCtx) commit(changes []retimed) {
	for _, ch := range changes {
		c.setRaw(c.lines[ch.index], ch.raw)
		line := float64(ch.index + 1)
		if ch.clamped {
			c.warn(EditWarningCodeTimeClamped, &line)
		}
		if ch.upgraded {
			c.warn(EditWarningCodeStyleUpgraded, &line)
		}
	}
}

// touched are the lines whose neighbourhood canonical must check after an edit; all means
// every line (sort-by-time).
type touched struct {
	all   bool
	lines []int
}

func (c *editCtx) setTime(edit jsonValue) (touched, bool) {
	index, ok := c.lineIndex(edit.get("line"))
	if !ok {
		return touched{}, false
	}
	value, ok := jsFinite(edit.get("seconds"))
	if !ok {
		return touched{}, false
	}
	change, ok := c.retime(index, value, c.oneDigitHours())
	if !ok {
		return touched{}, false
	}
	c.commit([]retimed{change})
	return touched{}, true // a timed line is never empty and keeps its eol
}

func (c *editCtx) shift(edit jsonValue) (touched, bool) {
	delta, ok := jsFinite(edit.get("seconds"))
	if !ok {
		return touched{}, false
	}
	var targets []int
	if list, present := edit.get("lines"); present {
		if list.kind != jsonArray {
			return touched{}, false
		}
		for _, v := range list.arr {
			i, ok := c.lineIndex(v, true)
			if !ok {
				return touched{}, false
			}
			targets = append(targets, i)
		}
		slices.Sort(targets)
		targets = slices.Compact(targets)
	} else {
		from := 0
		if v, present := edit.get("from"); present {
			if from, ok = c.lineIndex(v, true); !ok {
				return touched{}, false
			}
		}
		for i := from; i < len(c.lines); i++ {
			targets = append(targets, i)
		}
	}
	oneDigit := c.oneDigitHours()
	var changes []retimed
	for _, i := range targets {
		t := c.lines[i].scan.time
		if t == nil {
			continue
		}
		change, ok := c.retime(i, t.Seconds+delta, oneDigit)
		if !ok {
			return touched{}, false
		}
		changes = append(changes, change)
	}
	c.commit(changes)
	return touched{}, true
}

// timeBlock is a timed line plus the untimed lines after it.
type timeBlock struct {
	seconds  float64
	from, to int
}

// sortByTime is R13.9.4: a stable sort of the blocks.
func (c *editCtx) sortByTime() touched {
	first := slices.IndexFunc(c.lines, func(l *workLine) bool { return l.scan.time != nil })
	if first < 0 {
		return touched{}
	}
	var blocks []timeBlock
	for i := first; i < len(c.lines); i++ {
		if t := c.lines[i].scan.time; t != nil {
			blocks = append(blocks, timeBlock{seconds: t.Seconds, from: i, to: i + 1})
		} else if len(blocks) > 0 {
			blocks[len(blocks)-1].to = i + 1
		}
	}
	// Stable: equal times keep their order.
	slices.SortStableFunc(blocks, func(x, y timeBlock) int { return cmp.Compare(x.seconds, y.seconds) })
	order := make([]int, 0, len(c.lines))
	for i := 0; i < first; i++ {
		order = append(order, i)
	}
	for _, b := range blocks {
		for i := b.from; i < b.to; i++ {
			order = append(order, i)
		}
	}
	old := c.lines
	lines := make([]*workLine, 0, len(old))
	for slot, src := range order {
		if src < 0 || src >= len(old) || slot >= len(old) {
			continue
		}
		l := old[src]
		lines = append(lines, &workLine{
			raw:     l.raw,
			eol:     old[slot].eol, // eol values stay in their slots
			changed: l.changed || src != slot,
			scan:    l.scan,
		})
	}
	c.lines = lines
	return touched{all: true}
}

func (c *editCtx) setRest(edit jsonValue) (touched, bool) {
	index, ok := c.lineIndex(edit.get("line"))
	if !ok {
		return touched{}, false
	}
	rest, ok := singleLine(edit.get("rest"))
	if !ok {
		return touched{}, false
	}
	l := c.lines[index]
	p := l.scan.parts
	// R13.9 set-rest: a non-empty rest after a time is preceded by whitespace; a gap that does
	// not end with R0.2 whitespace ("", "]", ")") gets one space.
	gap := []rune(p.Gap)
	spaced := len(gap) > 0 && isWhitespace(gap[len(gap)-1])
	space := ""
	if p.Time != "" && !spaced && rest != "" {
		space = " "
	}
	c.setRaw(l, p.Lead+p.Time+p.Gap+space+rest)
	return touched{lines: []int{index}}, true
}

// commonEol is R13.9.5: the most common non-empty eol (ties: the one seen first in line order),
// "\n" if none. The counts are kept up to date; only a tie needs a look at the lines.
func (c *editCtx) commonEol() Eol {
	best := max(c.eols[0], c.eols[1], c.eols[2])
	if best <= 0 {
		return EolLF
	}
	isTop := func(eol Eol) bool {
		k := eolSlot(eol)
		return k >= 0 && c.eols[k] == best
	}
	var top []Eol
	for _, e := range []Eol{EolLF, EolCRLF, EolCR} {
		if isTop(e) {
			top = append(top, e)
		}
	}
	if len(top) == 1 {
		return top[0]
	}
	for _, l := range c.lines {
		if isTop(l.eol) {
			return l.eol
		}
	}
	return EolLF
}

func (c *editCtx) insert(edit jsonValue) (touched, bool) {
	after, ok := jsInteger(edit.get("after"))
	if !ok || after < 0 || after > float64(len(c.lines)) {
		return touched{}, false
	}
	text, ok := singleLine(edit.get("text"))
	if !ok {
		return touched{}, false
	}
	pos := int(after)
	eol := c.commonEol()
	count := len(c.lines)
	// R13.9.5: inserting into an empty text keeps it without a trailing newline.
	lineEol := eol
	if count == 0 {
		lineEol = EolNone
	}
	line := newWorkLine(text, lineEol, true)
	if pos == count && count > 0 && c.lines[count-1].eol == EolNone {
		c.setEol(c.lines[count-1], eol)
		line.eol = EolNone
	}
	c.countLine(line, 1)
	c.lines = slices.Insert(c.lines, pos, line)
	return touched{lines: []int{pos}}, true
}

func (c *editCtx) remove(edit jsonValue) (touched, bool) {
	index, ok := c.lineIndex(edit.get("line"))
	if !ok {
		return touched{}, false
	}
	gone := c.lines[index]
	c.lines = slices.Delete(c.lines, index, index+1)
	c.countLine(gone, -1)
	count := len(c.lines)
	if index == count && count > 0 && c.lines[count-1].eol != gone.eol {
		c.setEol(c.lines[count-1], gone.eol) // R13.9.5: the deleted last line's eol moves up
	}
	return touched{lines: []int{index}}, true
}

// mergeAt joins a "\r" line followed by an empty "\n" line into one CRLF line, as a re-split
// would read them.
func (c *editCtx) mergeAt(k int) {
	if k < 0 || k+1 >= len(c.lines) {
		return
	}
	prev, next := c.lines[k], c.lines[k+1]
	if prev.eol != EolCR || next.raw != "" || next.eol != EolLF {
		return
	}
	c.setEol(prev, EolCRLF)
	c.lines = slices.Delete(c.lines, k+1, k+2)
	c.countLine(next, -1)
}

// canonical is R13.9.5: keep the work lines equal to what splitLines(join(lines)) would give.
// A "\r" followed by an empty "\n" line reads back as one CRLF, and an empty last line without
// eol is not a line. The lines are canonical before each edit, so only the neighbourhood of
// the lines the edit touched (and the end) is checked; all checks every line.
func (c *editCtx) canonical(t touched) {
	count := len(c.lines)
	// Descending, so a merge at k never moves a pair still to be checked.
	var starts []int
	if t.all {
		for k := count - 2; k >= 0; k-- {
			starts = append(starts, k)
		}
	} else {
		starts = append(starts, count-2)
		for _, x := range t.lines {
			starts = append(starts, x-1, x)
		}
		slices.Sort(starts)
		starts = slices.Compact(starts)
		slices.Reverse(starts)
	}
	for _, k := range starts {
		c.mergeAt(k)
	}
	if n := len(c.lines); n > 0 {
		if last := c.lines[n-1]; last.raw == "" && last.eol == EolNone {
			c.lines = c.lines[:n-1]
			c.countLine(last, -1)
		}
	}
}

// applyOne applies one edit; false when it is invalid (R13.9.6).
func (c *editCtx) applyOne(edit jsonValue) (touched, bool) {
	if edit.kind != jsonObject {
		return touched{}, false
	}
	op, _ := edit.getString("op")
	switch op {
	case "set-time":
		return c.setTime(edit)
	case "shift":
		return c.shift(edit)
	case "sort-by-time":
		return c.sortByTime(), true
	case "set-rest":
		return c.setRest(edit)
	case "insert":
		return c.insert(edit)
	case "delete":
		return c.remove(edit)
	}
	return touched{}, false
}

// invalidLine is R13.9.6: the line of an edit-invalid warning is the edit's integer line, else
// its integer after, else none.
func invalidLine(edit jsonValue) *float64 {
	for _, key := range []string{"line", "after"} {
		if n, ok := jsInteger(edit.get(key)); ok {
			return &n
		}
	}
	return nil
}

// applyEdits is R13.9 on one (already sanitized) text: apply edits (loose JSON objects) in
// order. Never fails (R13.9.6): an invalid edit changes nothing and warns edit-invalid.
func applyEdits(text string, edits []jsonValue) rawEditResult {
	if len(edits) == 0 {
		return rawEditResult{text: text, changed: []int{}}
	}
	c := &editCtx{}
	for _, l := range splitLines(text) {
		line := newWorkLine(l.raw, l.eol, false)
		c.countLine(line, 1)
		c.lines = append(c.lines, line)
	}
	for i, edit := range edits {
		c.index = i
		before := len(c.warnings)
		// Validation fails before any mutation, so the lines need no rollback (R13.9.6).
		if t, ok := c.applyOne(edit); ok {
			c.canonical(t)
		} else {
			c.warnings = c.warnings[:before]
			c.warn(EditWarningCodeEditInvalid, invalidLine(edit))
		}
	}
	changed := []int{}
	var b strings.Builder
	for i, l := range c.lines {
		if l.changed {
			changed = append(changed, i+1)
		}
		b.WriteString(l.raw)
		b.WriteString(string(l.eol))
	}
	return rawEditResult{text: b.String(), changed: changed, warnings: c.warnings}
}
