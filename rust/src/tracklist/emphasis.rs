//! R13.12 Markdown emphasis markers in a tracklist line.

use crate::words::{at, before, is_ws};

/// Length of the maximal run of `*` or `_` at `from` (0 when there is none).
fn run_length(s: &[char], from: usize) -> usize {
    match at(s, from) {
        Some(ch @ ('*' | '_')) => {
            let mut j = from;
            while at(s, j) == Some(ch) {
                j += 1;
            }
            j - from
        }
        _ => 0,
    }
}

/// R13.2.1 / R13.2.3: the length of an emphasis run at `from` (a maximal run of 1 or 2 `*` or
/// `_`), else 0. Used next to the time, where runs are read by position rather than by pairing.
pub(crate) fn emphasis_run(s: &[char], from: usize) -> usize {
    let n = run_length(s, from);
    if n <= 2 {
        n
    } else {
        0
    }
}

/// R13.12.2 outer side: line edge, R0.2 whitespace, a bracket, or a `*` / `_` of another run.
fn is_outer(ch: Option<char>) -> bool {
    ch.map_or(true, |c| is_ws(c) || matches!(c, '[' | ']' | '(' | ')' | '*' | '_'))
}

/// R13.12.2 inner side: a character that is not R0.2 whitespace.
fn is_inner(ch: Option<char>) -> bool {
    ch.is_some_and(|c| !is_ws(c))
}

/// The stack of a run: one per `*`, `**`, `_`, `__`; longer runs and other characters have none.
fn stack_of(ch: Option<char>, len: usize) -> Option<usize> {
    match (ch, len) {
        (Some('*'), 1) => Some(0),
        (Some('*'), 2) => Some(1),
        (Some('_'), 1) => Some(2),
        (Some('_'), 2) => Some(3),
        _ => None,
    }
}

fn mark(marked: &mut [bool], from: usize, len: usize) {
    for slot in marked.iter_mut().skip(from).take(len) {
        *slot = true;
    }
}

/// R13.12: `raw[from..]` (the line's `rest`) without its emphasis markers. A run of 1 or 2 `*`
/// or `_` closes when the character before it is an inner side and the one after it an outer
/// side, and opens the other way round. A closer pairs with the nearest opener of the same
/// characters; that opener and every opener after it leave the stack. One stack per run (`*`,
/// `**`, `_`, `__`) keeps this linear. Runs before `from` (lead and gap) never pair, except that
/// `open`, a lead run the gap left open (R13.12.4), pairs with a closing run of the same
/// character at the end of the line.
pub(crate) fn without_markers(raw: &[char], from: usize, open: Option<char>) -> String {
    let mut marked = vec![false; raw.len()];
    let mut stacks: [Vec<usize>; 4] = [Vec::new(), Vec::new(), Vec::new(), Vec::new()];
    let mut i = from;
    while i < raw.len() {
        let len = run_length(raw, i).max(1);
        let own = stack_of(at(raw, i), len);
        let prev = before(raw, i);
        let next = at(raw, i + len);
        let opener = match own {
            Some(o) if is_inner(prev) && is_outer(next) => stacks.get_mut(o).and_then(Vec::pop),
            _ => None,
        };
        if let Some(a) = opener {
            mark(&mut marked, a, len);
            mark(&mut marked, i, len);
            for st in stacks.iter_mut() {
                while st.last().is_some_and(|&top| top > a) {
                    st.pop();
                }
            }
        } else if let Some(o) = own {
            if is_outer(prev) && is_inner(next) {
                if let Some(st) = stacks.get_mut(o) {
                    st.push(i);
                }
            }
        }
        i += len;
    }
    // R13.12.4: the last run of the line, after which there is only R0.2 whitespace.
    if let Some(open) = open {
        let mut end = raw.len();
        while end > from && before(raw, end).is_some_and(is_ws) {
            end -= 1;
        }
        let mut start = end;
        while start > from && before(raw, start) == Some(open) {
            start -= 1;
        }
        if end - start <= 2 && start > from && is_inner(before(raw, start)) {
            for slot in marked.iter_mut().take(end).skip(start) {
                if *slot {
                    break;
                }
                *slot = true;
            }
        }
    }
    raw.iter().zip(&marked).skip(from).filter(|(_, m)| !**m).map(|(c, _)| *c).collect()
}
