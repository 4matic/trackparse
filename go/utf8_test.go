package trackparse

// Invalid UTF-8 at the API boundary. Each maximal ill-formed subpart becomes one U+FFFD (the
// WHATWG rule), so a Go caller with bad bytes gets exactly what a Rust caller gets after
// String::from_utf8_lossy, and what JavaScript gets after TextDecoder. The expected strings
// below were printed by from_utf8_lossy (Rust 1.91) and by Node's TextDecoder, which agree.

import (
	"testing"
	"unicode/utf8"
)

var lossyCases = []struct{ bad, want string }{
	{"a\xffb", "a�b"},
	{"\xe2\x82", "�"},              // truncated three-byte sequence: one U+FFFD
	{"\xe2\x82A", "�A"},            // truncated, then ASCII
	{"\xed\xa0\x80", "���"},        // an encoded surrogate: one per byte
	{"\xf0\x9f\x98", "�"},          // truncated four-byte sequence
	{"\xf4\x90\x80\x80", "����"},   // above U+10FFFF
	{"\xc0\xaf", "��"},             // overlong
	{"\xe0\x80\xaf", "���"},        // overlong
	{"\xf0\x8f\xbf\xbf", "����"},   // overlong
	{"\x80\x80", "��"},             // lone continuation bytes
	{"\xf1\x80\x80", "�"},          // truncated
	{"\xe1\x80\xe2\x82\xac", "�€"}, // truncated, then a valid sequence
	{"Noisia \xff- Stigma \xf0\x9f\x98(VIP\xed\xbf\xbf)", "Noisia �- Stigma �(VIP���)"},
}

func TestSanitizeMatchesFromUTF8Lossy(t *testing.T) {
	for _, c := range lossyCases {
		if got := sanitize(c.bad); got != c.want {
			t.Errorf("sanitize(%q) = %q, want %q", c.bad, got, c.want)
		}
		if got := Normalize(c.bad); got != Normalize(c.want) {
			t.Errorf("Normalize(%q) = %q, want %q", c.bad, got, Normalize(c.want))
		}
	}
}

func TestInvalidUTF8ParsesAsTheLossyString(t *testing.T) {
	for _, c := range lossyCases {
		got, want := Parse(c.bad), Parse(c.want)
		if a, b := string(marshal(&got)), string(marshal(&want)); a != b {
			t.Errorf("Parse(%q):\n%s\nwant\n%s", c.bad, a, b)
		}
		if !utf8.ValidString(got.Input) {
			t.Errorf("Parse(%q).Input is not valid UTF-8", c.bad)
		}
		ga, wa := ParseArtists(c.bad), ParseArtists(c.want)
		if a, b := string(marshal(artistList(ga))), string(marshal(artistList(wa))); a != b {
			t.Errorf("ParseArtists(%q): %s, want %s", c.bad, a, b)
		}
		options := ParseOptions{Mode: ModeYouTube, Uploader: c.bad, KnownArtists: []string{c.bad}}
		lossy := ParseOptions{Mode: ModeYouTube, Uploader: c.want, KnownArtists: []string{c.want}}
		got, want = ParseWith(c.bad, options), ParseWith(c.want, lossy)
		if a, b := string(marshal(&got)), string(marshal(&want)); a != b {
			t.Errorf("ParseWith(%q) with bad options:\n%s\nwant\n%s", c.bad, a, b)
		}
	}
}

func TestInvalidUTF8TracklistIsTheLossyText(t *testing.T) {
	bad := "00:00 A\xc3 - B\r\n01:00 \xe2\x80\x93 C - D\xf0\n02:00 \xed\xa0\x80 - \xff\n"
	want := "00:00 A� - B\r\n01:00 – C - D�\n02:00 ��� - �\n"
	if got := sanitize(bad); got != want {
		t.Fatalf("sanitize = %q, want %q", got, want)
	}
	gl, wl := ParseTracklist(bad), ParseTracklist(want)
	if a, b := string(marshal(&gl)), string(marshal(&wl)); a != b {
		t.Errorf("ParseTracklist:\n%s\nwant\n%s", a, b)
	}
	if len(gl.Entries) != 3 {
		t.Errorf("%d entries, want 3", len(gl.Entries))
	}
	edits := []Edit{SetRest(1, "X\xff - Y"), Insert(3, "03:00 \xe2\x82 - Z"), Shift(5)}
	lossyEdits := []Edit{SetRest(1, "X� - Y"), Insert(3, "03:00 � - Z"), Shift(5)}
	ge, we := ApplyEdits(bad, edits), ApplyEdits(want, lossyEdits)
	if a, b := string(marshal(&ge)), string(marshal(&we)); a != b {
		t.Errorf("ApplyEdits:\n%s\nwant\n%s", a, b)
	}
	if !utf8.ValidString(ge.Text) {
		t.Errorf("ApplyEdits text is not valid UTF-8: %q", ge.Text)
	}
}
