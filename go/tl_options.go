package trackparse

import "math"

// R13.10 tracklist options are resolved in options.go (resolveTracklist, resolveTracklistJSON,
// jsNumber). This file holds the JS number tests the edits (R13.9) apply to loose JSON values.

// jsInteger is a JS number that Number.isInteger accepts.
func jsInteger(v jsonValue, present bool) (float64, bool) {
	n, ok := jsNumber(v, present)
	if !ok || math.IsNaN(n) || math.IsInf(n, 0) || n != math.Trunc(n) {
		return 0, false
	}
	return n, true
}

// jsFinite is a JS number that Number.isFinite accepts.
func jsFinite(v jsonValue, present bool) (float64, bool) {
	n, ok := jsNumber(v, present)
	if !ok || math.IsNaN(n) || math.IsInf(n, 0) {
		return 0, false
	}
	return n, true
}
