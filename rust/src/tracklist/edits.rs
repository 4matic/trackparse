//! R13.9: apply_edits. Edits apply in order, each to the result of the previous one.
//!
//! Edits are read as loose JSON objects with the number rules of the JS reference (a bool is not
//! a number; `null`, NaN and infinities are invalid). Typed [`Edit`](super::Edit) values are
//! written to that form first, so both doors share one code path.

use std::collections::BTreeSet;

use crate::json::Json;
use crate::words::is_ws;

use super::options::js_number;
use super::split::{has_line_break, split_lines};
use super::times::{format_time, scan_time, TimeScan};
use super::types::{num, EditResult, EditWarning, EditWarningCode, Eol};
use crate::types::obj;

struct WorkLine {
    raw: String,
    eol: Eol,
    /// Written by an edit (R13.9.1).
    changed: bool,
    /// `scan_time(raw)`, kept in step with `raw` (each line is scanned once per write).
    scan: TimeScan,
}

/// R13.9.6: the edit cannot be applied.
struct InvalidEdit;

type Checked<T> = Result<T, InvalidEdit>;

/// A warning with the line as the JS reference holds it: any integer-valued number an invalid
/// edit carried, not only a real line number.
pub(crate) struct RawEditWarning {
    code: EditWarningCode,
    edit: usize,
    line: Option<f64>,
}

/// The result of `apply_edits` before it is typed or written as JSON.
pub(crate) struct RawEditResult {
    text: String,
    changed: Vec<usize>,
    warnings: Vec<RawEditWarning>,
}

impl RawEditResult {
    pub(crate) fn into_result(self) -> EditResult {
        EditResult {
            text: self.text,
            changed: self.changed,
            warnings: self
                .warnings
                .into_iter()
                // Typed edits carry `usize` lines, written as JS numbers: the value is a whole
                // number from 0 to 2^64 and the cast saturates at `usize::MAX`. Negative and
                // larger lines only exist behind the JSON door, which writes `to_json_value`.
                .map(|w| EditWarning {
                    code: w.code,
                    edit: w.edit,
                    line: w.line.map(|l| l as usize),
                })
                .collect(),
        }
    }

    pub(crate) fn to_json_value(&self) -> Json {
        let warnings = self
            .warnings
            .iter()
            .map(|w| {
                obj(vec![
                    ("code", Json::str(w.code.as_str())),
                    ("edit", num(w.edit)),
                    ("line", w.line.map_or(Json::Null, Json::Num)),
                ])
            })
            .collect();
        obj(vec![
            ("text", Json::str(&self.text)),
            ("changed", Json::Arr(self.changed.iter().map(|l| num(*l)).collect())),
            ("warnings", Json::Arr(warnings)),
        ])
    }
}

/// Edit state. Every edit validates its input before it mutates anything, so an invalid edit
/// (R13.9.6) only has to drop the warnings it added: no snapshot of the lines is needed.
struct EditCtx {
    /// Boxed, so an insert or delete moves pointers, not lines.
    #[allow(clippy::vec_box)]
    lines: Vec<Box<WorkLine>>,
    index: usize,
    warnings: Vec<RawEditWarning>,
    /// R13.9.2: times with one-digit hours and with wider hours, over all lines.
    one_digit: i64,
    wide_hours: i64,
    /// R13.9.5: how many lines end with LF, CRLF and CR.
    eols: [i64; 3],
}

fn work_line(raw: String, eol: Eol, changed: bool) -> Box<WorkLine> {
    let scan = scan_time(&raw);
    Box::new(WorkLine { raw, eol, changed, scan })
}

fn eol_slot(eol: Eol) -> Option<usize> {
    match eol {
        Eol::Lf => Some(0),
        Eol::CrLf => Some(1),
        Eol::Cr => Some(2),
        Eol::None => None,
    }
}

impl EditCtx {
    /// Add (`by` = 1) or remove (`by` = -1) a line's time from the hour-width counts (R13.9.2).
    fn count_hours(&mut self, scan: &TimeScan, by: i64) {
        match &scan.style {
            Some(style) if style.hours => {
                if style.hour_width == 1 {
                    self.one_digit += by;
                } else {
                    self.wide_hours += by;
                }
            }
            _ => {}
        }
    }

    fn count_eol(&mut self, eol: Eol, by: i64) {
        if let Some(n) = eol_slot(eol).and_then(|slot| self.eols.get_mut(slot)) {
            *n += by;
        }
    }

    /// A line enters (`by` = 1) or leaves (`by` = -1) the text.
    fn count_line(&mut self, line: &WorkLine, by: i64) {
        self.count_hours(&line.scan, by);
        self.count_eol(line.eol, by);
    }

    fn set_eol(&mut self, index: usize, eol: Eol) {
        let Some(line) = self.lines.get_mut(index) else { return };
        let old = line.eol;
        line.eol = eol;
        line.changed = true;
        self.count_eol(old, -1);
        self.count_eol(eol, 1);
    }

    fn set_raw(&mut self, index: usize, raw: String) {
        let scan = scan_time(&raw);
        let Some(line) = self.lines.get_mut(index) else { return };
        let old = std::mem::replace(&mut line.scan, scan.clone());
        line.raw = raw;
        line.changed = true;
        self.count_hours(&old, -1);
        self.count_hours(&scan, 1);
    }

    fn warn(&mut self, code: EditWarningCode, line: Option<f64>) {
        self.warnings.push(RawEditWarning { code, edit: self.index, line });
    }

    /// 0-based index of an existing 1-based line number, else `InvalidEdit`.
    fn line_index(&self, value: Option<&Json>) -> Checked<usize> {
        let n = integer(value)?;
        if n < 1.0 || n > self.lines.len() as f64 {
            return Err(InvalidEdit);
        }
        Ok(n as usize - 1)
    }

    /// R13.9.2: do the text's times with hours use one-digit hours? (All of them, at least one.)
    fn one_digit_hours(&self) -> bool {
        self.one_digit > 0 && self.wide_hours == 0
    }
}

/// A JS number that `Number.isInteger` accepts.
fn integer(value: Option<&Json>) -> Checked<f64> {
    js_number(value).filter(|n| n.is_finite() && n.fract() == 0.0).ok_or(InvalidEdit)
}

fn finite(value: Option<&Json>) -> Checked<f64> {
    js_number(value).filter(|n| n.is_finite()).ok_or(InvalidEdit)
}

fn single_line(value: Option<&Json>) -> Checked<&str> {
    value.and_then(Json::as_str).filter(|s| !has_line_break(s)).ok_or(InvalidEdit)
}

struct Retime {
    index: usize,
    raw: String,
    clamped: bool,
    upgraded: bool,
}

/// R13.9.2 / R13.9.3: the rewritten line for a new value, or `InvalidEdit`.
fn retime(ctx: &EditCtx, index: usize, value: f64, one_digit: bool) -> Checked<Retime> {
    let scan = &ctx.lines.get(index).ok_or(InvalidEdit)?.scan;
    let (Some(style), Some(_)) = (&scan.style, &scan.time) else { return Err(InvalidEdit) };
    let clamped = value < 0.0;
    let f = format_time(if clamped { 0.0 } else { value }, style, one_digit).ok_or(InvalidEdit)?;
    let p = &scan.parts;
    Ok(Retime {
        index,
        raw: format!("{}{}{}{}", p.lead, f.raw, p.gap, p.rest),
        clamped,
        upgraded: f.upgraded,
    })
}

fn commit(ctx: &mut EditCtx, changes: Vec<Retime>) {
    for c in changes {
        ctx.set_raw(c.index, c.raw);
        let line = Some((c.index + 1) as f64);
        if c.clamped {
            ctx.warn(EditWarningCode::TimeClamped, line);
        }
        if c.upgraded {
            ctx.warn(EditWarningCode::StyleUpgraded, line);
        }
    }
}

/// Lines whose neighbourhood `canonical` must check after an edit; `None` = every line.
type Touched = Option<Vec<usize>>;

fn set_time(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    let index = ctx.line_index(edit.get("line"))?;
    let value = finite(edit.get("seconds"))?;
    let change = retime(ctx, index, value, ctx.one_digit_hours())?;
    commit(ctx, vec![change]);
    Ok(Some(Vec::new())) // a timed line is never empty and keeps its eol
}

fn shift(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    let delta = finite(edit.get("seconds"))?;
    let targets: Vec<usize> = if let Some(list) = edit.get("lines") {
        let list = list.as_array().ok_or(InvalidEdit)?;
        let set: BTreeSet<usize> =
            list.iter().map(|v| ctx.line_index(Some(v))).collect::<Checked<_>>()?;
        set.into_iter().collect()
    } else {
        let from = match edit.get("from") {
            Some(v) => ctx.line_index(Some(v))?,
            None => 0,
        };
        (from..ctx.lines.len()).collect()
    };
    let one_digit = ctx.one_digit_hours();
    let mut changes = Vec::new();
    for i in targets {
        let seconds = ctx.lines.get(i).and_then(|l| l.scan.time.as_ref()).map(|t| t.seconds);
        if let Some(seconds) = seconds {
            changes.push(retime(ctx, i, seconds + delta, one_digit)?);
        }
    }
    commit(ctx, changes);
    Ok(Some(Vec::new()))
}

struct Block {
    seconds: f64,
    from: usize,
    to: usize,
}

/// R13.9.4: stable sort of blocks (a timed line plus the untimed lines after it).
fn sort_by_time(ctx: &mut EditCtx) -> Touched {
    let Some(first) = ctx.lines.iter().position(|l| l.scan.time.is_some()) else {
        return Some(Vec::new());
    };
    let mut blocks: Vec<Block> = Vec::new();
    for (i, line) in ctx.lines.iter().enumerate().skip(first) {
        match (&line.scan.time, blocks.last_mut()) {
            (Some(t), _) => blocks.push(Block { seconds: t.seconds, from: i, to: i + 1 }),
            (None, Some(block)) => block.to = i + 1,
            (None, None) => {}
        }
    }
    // Stable: equal times keep their order.
    blocks.sort_by(|x, y| x.seconds.partial_cmp(&y.seconds).unwrap_or(std::cmp::Ordering::Equal));
    let order: Vec<usize> = (0..first).chain(blocks.iter().flat_map(|b| b.from..b.to)).collect();
    let eols: Vec<Eol> = ctx.lines.iter().map(|l| l.eol).collect();
    let mut old: Vec<Option<Box<WorkLine>>> =
        std::mem::take(&mut ctx.lines).into_iter().map(Some).collect();
    for (slot, src) in order.into_iter().enumerate() {
        let Some(mut line) = old.get_mut(src).and_then(Option::take) else { continue };
        line.eol = eols.get(slot).copied().unwrap_or(line.eol); // eol values stay in their slots
        line.changed = line.changed || src != slot;
        ctx.lines.push(line);
    }
    None
}

fn set_rest(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    let index = ctx.line_index(edit.get("line"))?;
    let rest = single_line(edit.get("rest"))?;
    let p = &ctx.lines.get(index).ok_or(InvalidEdit)?.scan.parts;
    // R13.9 set-rest: a non-empty rest after a time is preceded by whitespace; a gap that does
    // not end with R0.2 whitespace (`""`, `]`, `)`) gets one space.
    let spaced = p.gap.chars().next_back().is_some_and(is_ws);
    let space = if !p.time.is_empty() && !spaced && !rest.is_empty() { " " } else { "" };
    let raw = format!("{}{}{}{space}{rest}", p.lead, p.time, p.gap);
    ctx.set_raw(index, raw);
    Ok(Some(vec![index]))
}

/// R13.9.5: the most common non-empty eol (ties: the one seen first in line order), `"\n"` if
/// none. Counts are kept up to date; only a tie needs a look at the lines.
fn common_eol(ctx: &EditCtx) -> Eol {
    let best = ctx.eols.iter().copied().max().unwrap_or(0);
    if best <= 0 {
        return Eol::Lf;
    }
    let is_top = |eol: Eol| eol_slot(eol).and_then(|s| ctx.eols.get(s)).is_some_and(|n| *n == best);
    let top: Vec<Eol> = [Eol::Lf, Eol::CrLf, Eol::Cr].into_iter().filter(|e| is_top(*e)).collect();
    match top.as_slice() {
        [only] => *only,
        _ => ctx.lines.iter().map(|l| l.eol).find(|e| is_top(*e)).unwrap_or(Eol::Lf),
    }
}

fn insert(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    let after = integer(edit.get("after"))?;
    if after < 0.0 || after > ctx.lines.len() as f64 {
        return Err(InvalidEdit);
    }
    let after = after as usize;
    let text = single_line(edit.get("text"))?;
    let eol = common_eol(ctx);
    let count = ctx.lines.len();
    // R13.9.5: inserting into an empty text keeps it without a trailing newline.
    let mut line = work_line(text.to_string(), if count == 0 { Eol::None } else { eol }, true);
    if after == count && ctx.lines.last().is_some_and(|last| last.eol == Eol::None) {
        ctx.set_eol(count - 1, eol);
        line.eol = Eol::None;
    }
    ctx.count_line(&line, 1);
    ctx.lines.insert(after, line);
    Ok(Some(vec![after]))
}

fn remove(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    let index = ctx.line_index(edit.get("line"))?;
    let gone = ctx.lines.remove(index);
    ctx.count_line(&gone, -1);
    let count = ctx.lines.len();
    if index == count && ctx.lines.last().is_some_and(|last| last.eol != gone.eol) {
        ctx.set_eol(count - 1, gone.eol); // R13.9.5: the deleted last line's eol moves up
    }
    Ok(Some(vec![index]))
}

/// A `"\r"` line followed by an empty `"\n"` line reads back as one CRLF line.
fn merge_at(ctx: &mut EditCtx, k: usize) {
    let (Some(prev), Some(next)) = (ctx.lines.get(k), ctx.lines.get(k + 1)) else { return };
    if prev.eol != Eol::Cr || !next.raw.is_empty() || next.eol != Eol::Lf {
        return;
    }
    ctx.set_eol(k, Eol::CrLf);
    let next = ctx.lines.remove(k + 1);
    ctx.count_line(&next, -1);
}

/// R13.9.5: keep the work lines equal to what `split_lines(join(lines))` would give: a `"\r"`
/// followed by an empty `"\n"` line reads back as one CRLF, and an empty last line without eol is
/// not a line. The lines are canonical before each edit, so only the neighbourhood of the lines
/// the edit touched (and the end) is checked; `None` checks every line (sort-by-time).
fn canonical(ctx: &mut EditCtx, touched: Touched) {
    let count = ctx.lines.len();
    // Descending, so a merge at k never moves a pair still to be checked.
    let starts: Vec<usize> = match touched {
        None => (0..count.saturating_sub(1)).rev().collect(),
        Some(touched) => {
            let mut set: BTreeSet<usize> = BTreeSet::new();
            set.extend(count.checked_sub(2));
            for t in touched {
                set.extend(t.checked_sub(1));
                set.insert(t);
            }
            set.into_iter().rev().collect()
        }
    };
    for k in starts {
        merge_at(ctx, k);
    }
    if ctx.lines.last().is_some_and(|last| last.raw.is_empty() && last.eol == Eol::None) {
        if let Some(last) = ctx.lines.pop() {
            ctx.count_line(&last, -1);
        }
    }
}

fn apply_one(ctx: &mut EditCtx, edit: &Json) -> Checked<Touched> {
    if edit.as_object().is_none() {
        return Err(InvalidEdit);
    }
    match edit.get("op").and_then(Json::as_str) {
        Some("set-time") => set_time(ctx, edit),
        Some("shift") => shift(ctx, edit),
        Some("sort-by-time") => Ok(sort_by_time(ctx)),
        Some("set-rest") => set_rest(ctx, edit),
        Some("insert") => insert(ctx, edit),
        Some("delete") => remove(ctx, edit),
        _ => Err(InvalidEdit),
    }
}

/// R13.9.6: an `edit-invalid` warning's line is the edit's integer `line`, else `after`, else
/// none.
fn invalid_line(edit: &Json) -> Option<f64> {
    ["line", "after"].into_iter().find_map(|key| integer(edit.get(key)).ok())
}

/// R13.9: apply `edits` (loose JSON objects) to `text`. Never fails (R13.9.6): an invalid edit
/// changes nothing and warns `edit-invalid`.
pub(crate) fn apply_edits(text: &str, edits: &[Json]) -> RawEditResult {
    if edits.is_empty() {
        return RawEditResult { text: text.to_string(), changed: Vec::new(), warnings: Vec::new() };
    }
    let mut ctx = EditCtx {
        lines: Vec::new(),
        index: 0,
        warnings: Vec::new(),
        one_digit: 0,
        wide_hours: 0,
        eols: [0; 3],
    };
    for l in split_lines(text) {
        let line = work_line(l.raw, l.eol, false);
        ctx.count_line(&line, 1);
        ctx.lines.push(line);
    }
    for (i, edit) in edits.iter().enumerate() {
        ctx.index = i;
        let warnings_before = ctx.warnings.len();
        // Validation fails before any mutation, so the lines need no rollback (R13.9.6).
        match apply_one(&mut ctx, edit) {
            Ok(touched) => canonical(&mut ctx, touched),
            Err(InvalidEdit) => {
                ctx.warnings.truncate(warnings_before);
                ctx.warn(EditWarningCode::EditInvalid, invalid_line(edit));
            }
        }
    }
    let changed =
        ctx.lines.iter().enumerate().filter(|(_, l)| l.changed).map(|(i, _)| i + 1).collect();
    let mut text = String::new();
    for l in &ctx.lines {
        text.push_str(&l.raw);
        text.push_str(l.eol.as_str());
    }
    RawEditResult { text, changed, warnings: ctx.warnings }
}
