package trackparse

import (
	"math"
	"strconv"
	"strings"
)

// R13.2: the tracklist time scanner (separate from R4.1), and R13.9.2 time formatting.

// maxSafeInteger is JS Number.MAX_SAFE_INTEGER.
const maxSafeInteger = 9007199254740991.0

// timeStyle is the written shape of a time, kept by edits (R13.9.2).
type timeStyle struct {
	hours bool
	// hourWidth is the number of digits written for the hours (minimum width), 0 without hours.
	hourWidth int
	// minuteWidth is the number of digits written for the minutes (minimum width).
	minuteWidth int
	// longMinutes is R13.9.2: a two-part time already at 60 minutes or more; it stays two-part
	// when it grows.
	longMinutes bool
	// fracSep is '.' or ',' when the time has a fraction, else noRune.
	fracSep    rune
	fracDigits int
}

// timeScan is what R13.2 finds at the start of a line.
type timeScan struct {
	parts Parts
	time  *TracklistTime
	style *timeStyle
	// invalid is R13.2.4: a digits:digits shape after the lead that fails R13.2.2.
	invalid bool
	// text is R13.12: rest without its paired emphasis markers (the line text, before R13.13
	// and R13.5).
	text string
}

// isGapSeparator is an R13.2.3 separator after the time.
func isGapSeparator(r rune) bool {
	switch r {
	case '-', '–', '—', '|', '.', ':':
		return true
	}
	return false
}

// skipWhitespace is the index past the R0.2 whitespace at from.
func skipWhitespace(s []rune, from int) int {
	i := from
	for isWhitespace(at(s, i)) {
		i++
	}
	return i
}

// isBullet is an R13.2.1 bullet (before a time, or alone on a line without one).
func isBullet(r rune) bool {
	switch r {
	case '-', '–', '—', '*', '•':
		return true
	}
	return false
}

// untimed is R13.1.3 / R13.2.1: a line without a time. lead holds an optional bullet: R0.2
// whitespace, one of - – — * •, then at least one R0.2 whitespace (all of it); rest is the
// remainder.
func untimed(raw []rune, invalid bool) timeScan {
	start := skipWhitespace(raw, 0)
	lead := 0
	if isBullet(at(raw, start)) && isWhitespace(at(raw, start+1)) {
		lead = skipWhitespace(raw, start+1)
	}
	return timeScan{
		parts:   Parts{Lead: string(sub(raw, 0, lead)), Rest: string(tail(raw, lead))},
		invalid: invalid,
		text:    withoutMarkers(raw, lead, noRune),
	}
}

// closingRun is R13.2.3: the index past a closing run for an opening run of open (the same
// character, 1 or 2 long), or from when there is none.
func closingRun(raw []rune, from int, open rune) int {
	if open != noRune && at(raw, from) == open {
		return from + emphasisRun(raw, from)
	}
	return from
}

// secondsOf is whole.frac read as one decimal literal, so every port gets the same double (JS
// Number(), correctly rounded).
func secondsOf(whole int, frac string) float64 {
	if frac == "" {
		return float64(whole)
	}
	v, err := strconv.ParseFloat(strconv.Itoa(whole)+"."+frac, 64)
	if err != nil {
		return float64(whole)
	}
	return v
}

// timeShape is a digits(:digits)+ run: the digit runs between the colons (2 or more) and the
// index past it.
type timeShape struct {
	parts [][]rune
	end   int
}

// shapeAt is R13.2.4: the maximal digits(:digits)+ run at from.
func shapeAt(s []rune, from int) (timeShape, bool) {
	na := digitRun(s, from)
	if na == 0 || at(s, from+na) != ':' || !isDigit(at(s, from+na+1)) {
		return timeShape{}, false
	}
	parts := [][]rune{sub(s, from, from+na)}
	end := from + na
	for at(s, end) == ':' && isDigit(at(s, end+1)) {
		n := digitRun(s, end+1)
		parts = append(parts, sub(s, end+1, end+1+n))
		end += 1 + n
	}
	return timeShape{parts: parts, end: end}, true
}

// shapeSeconds is the R13.2.2 validity test: the whole seconds of A:B or A:B:C (no other part
// count).
func shapeSeconds(parts [][]rune) (int, bool) {
	switch len(parts) {
	case 3:
		a, b, c := parts[0], parts[1], parts[2]
		if len(a) == 0 || len(a) > 2 || len(b) != 2 || len(c) != 2 {
			return 0, false
		}
		m, sec := digitsValue(b), digitsValue(c)
		if m > 59 || sec > 59 {
			return 0, false
		}
		return digitsValue(a)*3600 + m*60 + sec, true
	case 2:
		a, b := parts[0], parts[1]
		if len(a) == 0 || len(a) > 3 || len(b) != 2 {
			return 0, false
		}
		sec := digitsValue(b)
		if sec > 59 {
			return 0, false
		}
		return digitsValue(a)*60 + sec, true
	}
	return 0, false
}

// startsWithAt reports whether s has word at from.
func startsWithAt(s []rune, from int, word string) bool {
	w := []rune(word)
	got := sub(s, from, from+len(w))
	if len(got) != len(w) {
		return false
	}
	for k := range w {
		if got[k] != w[k] {
			return false
		}
	}
	return true
}

// scanTime is R13.2: the lead, time and gap at the start of a line; the rest is everything
// after.
func scanTime(line string) timeScan {
	raw := []rune(line)
	// R13.2.1 lead: whitespace, an optional bullet, an optional emphasis run, an optional '[' or
	// '(', then (after a bracket) another optional emphasis run. A run counts only when the
	// next character is a bracket (first run) or a digit.
	i := skipWhitespace(raw, 0)
	if isBullet(at(raw, i)) && isWhitespace(at(raw, i+1)) {
		i = skipWhitespace(raw, i+1)
	}
	outerOpen := noRune
	n := emphasisRun(raw, i)
	if next := at(raw, i+n); n > 0 && (next == '[' || next == '(' || isDigit(next)) {
		outerOpen = at(raw, i)
		i += n
	}
	closer := noRune
	switch at(raw, i) {
	case '[':
		closer = ']'
	case '(':
		closer = ')'
	}
	innerOpen := noRune
	if closer != noRune {
		i++
		n = emphasisRun(raw, i)
		if n > 0 && isDigit(at(raw, i+n)) {
			innerOpen = at(raw, i)
			i += n
		}
	}
	leadEnd := i

	// R13.2.2 time
	shape, ok := shapeAt(raw, i)
	if !ok {
		return untimed(raw, false)
	}
	whole, ok := shapeSeconds(shape.parts)
	if !ok {
		return untimed(raw, true) // R13.2.4
	}
	j := shape.end
	fracSep := noRune
	frac := ""
	if sep := at(raw, j); sep == '.' || sep == ',' {
		if n := digitRun(raw, j+1); n >= 1 && n <= 3 {
			fracSep = sep
			frac = string(sub(raw, j+1, j+1+n))
			j += 1 + n
		}
	}
	timeEnd := j

	// R13.2.3 gap: closing runs for the lead's runs (the same character), around the closing
	// bracket.
	followsTime := func(k int) bool {
		return k >= len(raw) || isWhitespace(at(raw, k)) || isGapSeparator(at(raw, k))
	}
	// R13.12.4: a lead run the gap leaves open may pair with a run at the end of the line.
	open := noRune
	if closer != noRune {
		k := closingRun(raw, j, innerOpen)
		if k == j {
			open = innerOpen
		}
		j = k
		if at(raw, j) != closer {
			return untimed(raw, false)
		}
		j++
		k = closingRun(raw, j, outerOpen)
		if k > j && followsTime(k) {
			j = k
		} else if outerOpen != noRune {
			open = outerOpen
		}
	} else {
		k := closingRun(raw, j, outerOpen)
		if k == j {
			open = outerOpen
		}
		j = k
		if !followsTime(j) {
			return untimed(raw, false)
		}
	}
	j = skipWhitespace(raw, j)
	if isGapSeparator(at(raw, j)) {
		j = skipWhitespace(raw, j+1)
	}
	if startsWithAt(raw, j, "==>") {
		j = skipWhitespace(raw, j+3)
	} else if startsWithAt(raw, j, "=>") {
		j = skipWhitespace(raw, j+2)
	}

	timeRaw := string(sub(raw, leadEnd, timeEnd))
	hours := len(shape.parts) == 3
	var first, second []rune
	if len(shape.parts) > 0 {
		first = shape.parts[0]
	}
	if len(shape.parts) > 1 {
		second = shape.parts[1]
	}
	// R13.9.2 long-minute style: two parts with 60 minutes or more.
	longMinutes := !hours && digitsValue(first) >= 60
	style := timeStyle{hours: hours, longMinutes: longMinutes, fracSep: fracSep, fracDigits: len([]rune(frac))}
	switch {
	case hours:
		style.hourWidth = len(first)
		style.minuteWidth = len(second)
	case longMinutes:
		// R13.9.2: a long-minute time's minimum minute width is min(width, 2), so it never
		// keeps 3 minute digits once it is short again (125:30 minus 7000 s gives 08:50).
		style.minuteWidth = min(len(first), 2)
	default:
		style.minuteWidth = len(first)
	}
	return timeScan{
		parts: Parts{
			Lead: string(sub(raw, 0, leadEnd)),
			Time: timeRaw,
			Gap:  string(sub(raw, timeEnd, j)),
			Rest: string(tail(raw, j)),
		},
		time:  &TracklistTime{Raw: timeRaw, Seconds: secondsOf(whole, frac)},
		style: &style,
		text:  withoutMarkers(raw, j, open),
	}
}

// pad writes n with at least width digits.
func pad(n int64, width int) string {
	s := strconv.FormatInt(n, 10)
	if len(s) >= width {
		return s
	}
	return strings.Repeat("0", width-len(s)) + s
}

// formattedTime is a time written by formatTime.
type formattedTime struct {
	raw      string
	upgraded bool
}

// formatTime is R13.9.2: seconds (finite, not negative) written in style. Values are rounded
// half up to the fraction precision (floor(v * 10^d + 0.5), never math.Round). A value of an
// hour or more without hours is upgraded to HH:MM:SS (H:MM:SS when oneDigitHours), except in
// long-minute style. false when the value cannot be written as a valid R13.2.2 time (100 hours
// or more, more than 999 long-style minutes).
func formatTime(seconds float64, style *timeStyle, oneDigitHours bool) (formattedTime, bool) {
	scale := 1000.0
	switch style.fracDigits {
	case 0:
		scale = 1
	case 1:
		scale = 10
	case 2:
		scale = 100
	}
	// The same double arithmetic as the JS reference; integers only after the range check.
	units := math.Floor(seconds*scale + 0.5)
	if math.IsNaN(units) || math.IsInf(units, 0) || math.Abs(units) > maxSafeInteger || units < 0 {
		return formattedTime{}, false
	}
	wholeF := math.Floor(units / scale)
	fracUnits := int64(units - wholeF*scale)
	// The written time must stay valid under R13.2.2 (hours are 1 or 2 digits).
	if wholeF >= 100*3600 {
		return formattedTime{}, false
	}
	whole := int64(wholeF)
	frac := ""
	if style.fracSep != noRune && style.fracDigits > 0 {
		frac = string(style.fracSep) + pad(fracUnits, style.fracDigits)
	}
	sec := whole % 60
	if style.hours {
		h, m := whole/3600, (whole%3600)/60
		return formattedTime{
			raw: pad(h, style.hourWidth) + ":" + pad(m, style.minuteWidth) + ":" + pad(sec, 2) + frac,
		}, true
	}
	if style.longMinutes {
		m := whole / 60
		if m > 999 {
			return formattedTime{}, false // R13.2.2: two-part minutes are 1 to 3 digits
		}
		return formattedTime{raw: pad(m, style.minuteWidth) + ":" + pad(sec, 2) + frac}, true
	}
	if whole >= 3600 {
		h, m := whole/3600, (whole%3600)/60
		width := 2
		if oneDigitHours {
			width = 1
		}
		return formattedTime{
			raw:      pad(h, width) + ":" + pad(m, 2) + ":" + pad(sec, 2) + frac,
			upgraded: true,
		}, true
	}
	return formattedTime{raw: pad(whole/60, style.minuteWidth) + ":" + pad(sec, 2) + frac}, true
}
