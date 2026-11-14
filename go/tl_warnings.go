package trackparse

import "slices"

// R13.7: tracklist warnings and the time-out-of-order suggestion search.

// warningInput is what tracklistWarnings reads, one slot per entry.
type warningInput struct {
	entries []Entry
	// positions is the R4.2 / R13.5 position number of each entry's track, or -1.
	positions []int
	// invalid is R13.2.4 per entry.
	invalid []bool
	// unparsed is R13.7 line-unparsed per entry: a track entry whose track part has no
	// separator (R13.3).
	unparsed []bool
	// audioSeconds is nil when unknown.
	audioSeconds *float64
}

// digitSlot is one digit of a time that the search may change.
type digitSlot struct {
	// at is the index of the digit in the raw time.
	at    int
	field int
	place int64
	digit int64
}

// digitChange is one digit change: the slot and its new digit.
type digitChange struct {
	slot  digitSlot
	digit int64
}

// searchOutcome is the result of one line's digit search: no candidate, exactly one, or
// several.
type searchOutcome uint8

const (
	searchNone searchOutcome = iota
	searchOne
	searchMany
)

// timeDigits is a raw time taken apart for the search.
type timeDigits struct {
	raw    []rune
	frac   string
	values []int64
	lo, hi *float64
}

// valueWith is the value with changes applied, when it is a valid time inside the bounds.
func (d *timeDigits) valueWith(changes []digitChange) (float64, bool) {
	// A time has at most three fields; the copy stays on the stack (the search calls this up to
	// about 1,800 times per line).
	var buf [4]int64
	v := append(buf[:0], d.values...)
	for _, c := range changes {
		if c.slot.field >= 0 && c.slot.field < len(v) {
			v[c.slot.field] += (c.digit - c.slot.digit) * c.slot.place
		}
	}
	mult := [3]int64{60, 1, 0}
	if len(v) == 3 {
		mult = [3]int64{3600, 60, 1}
	}
	var whole int64
	for f, value := range v {
		if f > 0 && value > 59 {
			return 0, false
		}
		m := int64(0)
		if f < len(mult) {
			m = mult[f]
		}
		whole += value * m
	}
	if whole < 0 || whole > 1<<32-1 {
		return 0, false
	}
	t := secondsOf(int(whole), d.frac)
	if (d.lo == nil || *d.lo < t) && (d.hi == nil || t < *d.hi) {
		return t, true
	}
	return 0, false
}

// render is the raw time with changes applied.
func (d *timeDigits) render(changes []digitChange) string {
	out := slices.Clone(d.raw)
	for _, c := range changes {
		if c.slot.at >= 0 && c.slot.at < len(out) && c.digit >= 0 && c.digit <= 9 {
			out[c.slot.at] = rune('0' + c.digit)
		}
	}
	return string(out)
}

// searchTime is the R13.7 suggestion search on one raw time: change 1, then 2 digits (fraction
// digits excluded); keep the candidates that are valid times (R13.2.2: fields after the first
// at most 59) with lo < t (when lo exists) and t < hi (when hi exists). Only the smallest number
// of changes with any candidate counts. Bounded and arithmetic: at most 7 digits, 9 * 7 +
// 81 * 21 candidates.
func searchTime(rawTime string, lo, hi *float64) (searchOutcome, TracklistTime) {
	raw := []rune(rawTime)
	fracStart := len(raw)
	for i, r := range raw {
		if r == '.' || r == ',' {
			fracStart = i
			break
		}
	}
	frac := string(tail(raw, fracStart+1))
	values := []int64{0}
	var slots []digitSlot
	for i := 0; i < fracStart; i++ {
		ch := raw[i]
		if ch == ':' {
			values = append(values, 0)
		} else if isDigit(ch) {
			d := int64(ch - '0')
			f := len(values) - 1
			values[f] = values[f]*10 + d
			slots = append(slots, digitSlot{at: i, field: f, digit: d})
		}
	}
	// The place value of each digit inside its field.
	place := int64(1)
	field := -1
	for k := len(slots) - 1; k >= 0; k-- {
		if slots[k].field != field {
			field = slots[k].field
			place = 1
		}
		slots[k].place = place
		place *= 10
	}
	digits := timeDigits{raw: raw, frac: frac, values: values, lo: lo, hi: hi}

	var found []digitChange
	count := 0
	for _, slot := range slots {
		for d := int64(0); d <= 9; d++ {
			if d == slot.digit {
				continue
			}
			change := [1]digitChange{{slot, d}}
			if _, ok := digits.valueWith(change[:]); ok {
				count++
				found = slices.Clone(change[:])
			}
		}
	}
	if count == 0 {
	pairs:
		for x, a := range slots {
			for _, b := range slots[x+1:] {
				for da := int64(0); da <= 9; da++ {
					if da == a.digit {
						continue
					}
					for db := int64(0); db <= 9; db++ {
						if db == b.digit {
							continue
						}
						change := [2]digitChange{{a, da}, {b, db}}
						if _, ok := digits.valueWith(change[:]); ok {
							count++
							found = slices.Clone(change[:])
							if count >= 2 {
								break pairs
							}
						}
					}
				}
			}
		}
	}
	switch {
	case found != nil && count == 1:
		seconds, ok := digits.valueWith(found)
		if !ok {
			return searchNone, TracklistTime{}
		}
		return searchOne, TracklistTime{Raw: digits.render(found), Seconds: seconds}
	case found != nil && count > 1:
		return searchMany, TracklistTime{}
	}
	return searchNone, TracklistTime{}
}

// timedEntry is a timed entry: its line number and time.
type timedEntry struct {
	line int
	time *TracklistTime
}

// suggestFix is the R13.7 suggestion for the out-of-order timed entry k (an index into times):
// search its own time between its neighbours first; only when that finds no candidate at all,
// search the previous timed entry's time between the one before it and entry k.
func suggestFix(times []timedEntry, k int) *Suggestion {
	if k < 1 || k >= len(times) {
		return nil
	}
	cur, prev := times[k], times[k-1]
	var next *float64
	if k+1 < len(times) {
		next = &times[k+1].time.Seconds
	}
	switch outcome, t := searchTime(cur.time.Raw, &prev.time.Seconds, next); outcome {
	case searchOne:
		return &Suggestion{Line: cur.line, Raw: t.Raw, Seconds: t.Seconds}
	case searchMany:
		return nil
	}
	var lo *float64
	if k >= 2 {
		lo = &times[k-2].time.Seconds
	}
	if outcome, t := searchTime(prev.time.Raw, lo, &cur.time.Seconds); outcome == searchOne {
		return &Suggestion{Line: prev.line, Raw: t.Raw, Seconds: t.Seconds}
	}
	return nil
}

// warningRank is the table order of R13.7; the warnings of one line are emitted in this order.
// It is a switch, not a lookup in AllWarningCodes, so no caller can change the order.
func warningRank(code WarningCode) int {
	switch code {
	case WarningCodeTimeMissing:
		return 0
	case WarningCodeTimeOutOfOrder:
		return 1
	case WarningCodeTimeDuplicate:
		return 2
	case WarningCodeNoStartAtZero:
		return 3
	case WarningCodeTimeAfterAudioEnd:
		return 4
	case WarningCodeInvalidTime:
		return 5
	case WarningCodePositionGap:
		return 6
	case WarningCodePositionDuplicate:
		return 7
	case WarningCodeLineUnparsed:
		return 8
	}
	return 9
}

// flagAt is list[i], false out of range.
func flagAt(list []bool, i int) bool {
	return i >= 0 && i < len(list) && list[i]
}

// tracklistWarnings is R13.7.
func tracklistWarnings(in *warningInput) []TracklistWarning {
	entries := in.entries
	perLine := make([][]TracklistWarning, len(entries))
	add := func(i int, code WarningCode, suggestion *Suggestion) {
		if i < 0 || i >= len(entries) {
			return
		}
		severity := SeverityWarning
		if code == WarningCodeNoStartAtZero {
			severity = SeverityInfo
		}
		perLine[i] = append(perLine[i], TracklistWarning{
			Code: code, Line: entries[i].Line, Severity: severity, Suggestion: suggestion,
		})
	}

	var timedIdx []int
	var times []timedEntry
	for i := range entries {
		if t := entries[i].Time; t != nil {
			timedIdx = append(timedIdx, i)
			times = append(times, timedEntry{line: entries[i].Line, time: t})
		}
	}

	for i := range entries {
		e := &entries[i]
		// R13.13.3: an untimed layered line plays over the previous track, at its time.
		if e.Kind == EntryKindTrack && e.Time == nil && !e.Layered && len(times) > 0 {
			add(i, WarningCodeTimeMissing, nil)
		}
		if flagAt(in.invalid, i) {
			add(i, WarningCodeInvalidTime, nil)
		}
		if flagAt(in.unparsed, i) {
			add(i, WarningCodeLineUnparsed, nil)
		}
	}

	var prev *float64
	for k, i := range timedIdx {
		t := times[k].time.Seconds
		switch {
		case prev == nil:
			if t != 0 {
				add(i, WarningCodeNoStartAtZero, nil)
			}
		case t < *prev:
			add(i, WarningCodeTimeOutOfOrder, suggestFix(times, k))
		case t == *prev:
			add(i, WarningCodeTimeDuplicate, nil)
		}
		if in.audioSeconds != nil && t > *in.audioSeconds {
			add(i, WarningCodeTimeAfterAudioEnd, nil)
		}
		prev = &times[k].time.Seconds
	}

	prevPos := -1
	for i := range entries {
		if entries[i].Kind != EntryKindTrack || i >= len(in.positions) || in.positions[i] < 0 {
			continue
		}
		pos := in.positions[i]
		if prevPos >= 0 {
			if pos == prevPos {
				add(i, WarningCodePositionDuplicate, nil)
			} else if prevPos+1 != pos {
				add(i, WarningCodePositionGap, nil)
			}
		}
		prevPos = pos
	}

	out := []TracklistWarning{}
	for _, ws := range perLine {
		slices.SortStableFunc(ws, func(a, b TracklistWarning) int {
			return warningRank(a.Code) - warningRank(b.Code)
		})
		out = append(out, ws...)
	}
	return out
}
