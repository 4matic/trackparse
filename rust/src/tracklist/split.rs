//! R13.1: line split (LF, CRLF, CR only) with the lossless guarantee.

use super::types::Eol;

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Line {
    pub raw: String,
    pub eol: Eol,
}

/// R13.1.1: split at LF, CRLF and CR. U+2028/U+2029, U+0085 and other R0.2 characters are not
/// line breaks. A trailing empty segment after the final terminator is not a line; `""` has no
/// lines. R13.1.2: the lines' `raw + eol`, joined, are the text.
pub(crate) fn split_lines(text: &str) -> Vec<Line> {
    let bytes = text.as_bytes();
    let mut lines = Vec::new();
    let mut start = 0;
    let mut i = 0;
    // LF and CR are ASCII, so every cut is at a character boundary.
    let piece = |a: usize, b: usize| text.get(a..b).unwrap_or("").to_string();
    while let Some(&c) = bytes.get(i) {
        if c == 0x0a {
            lines.push(Line { raw: piece(start, i), eol: Eol::Lf });
            start = i + 1;
        } else if c == 0x0d {
            if bytes.get(i + 1) == Some(&0x0a) {
                lines.push(Line { raw: piece(start, i), eol: Eol::CrLf });
                i += 1;
            } else {
                lines.push(Line { raw: piece(start, i), eol: Eol::Cr });
            }
            start = i + 1;
        }
        i += 1;
    }
    if start < bytes.len() {
        lines.push(Line { raw: piece(start, bytes.len()), eol: Eol::None });
    }
    lines
}

/// A line break inside a single-line value (R13.9.5, R13.9.6).
pub(crate) fn has_line_break(s: &str) -> bool {
    s.bytes().any(|c| c == 0x0a || c == 0x0d)
}
