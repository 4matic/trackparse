"""R13.1–R13.8: parse_tracklist."""

from __future__ import annotations

from dataclasses import replace
from typing import Optional

from ._data import SPEC_VERSION
from ._parser import LineProbe, ListParser
from ._tracklist_kinds import (
    has_marker_word,
    has_separator,
    is_blank,
    is_header,
    is_link,
    marker_table,
)
from ._tracklist_options import ResolvedOptions
from ._tracklist_positions import BareCandidate, bare_candidate, confirm_positions
from ._tracklist_split import Line, split_lines
from ._tracklist_times import TimeScan, scan_time
from ._tracklist_types import EntryKind, Stats, Tracklist, TracklistEntry
from ._tracklist_warnings import tracklist_warnings
from .types import ParsedTrack, Position


class _Row:
    __slots__ = (
        "bare",
        "blank",
        "candidate",
        "dash",
        "line",
        "marker",
        "probe",
        "scan",
        "separated",
        "track",
    )

    def __init__(self, line: Line, scan: TimeScan, blank: bool) -> None:
        self.line = line
        self.scan = scan
        self.blank = blank
        #: R13.4.2
        self.candidate = False
        self.dash = False
        self.probe: Optional[LineProbe] = None
        self.bare: Optional[BareCandidate] = None
        self.track: Optional[ParsedTrack] = None
        #: R13.3.4: the track part has a separator.
        self.separated = False
        #: R13.3.5: the track part has a marker word.
        self.marker = False


def parse_tracklist_resolved(text: str, opts: ResolvedOptions) -> Tracklist:
    markers = marker_table(opts.marker_words)
    lines = split_lines(text)
    lp = ListParser(opts.parse)

    # R13.2 time scan. R13.4.2: a candidate line has a time or a spaced dash in the skeleton R6
    # searches (after R2/R4); the probe runs R1/R2/R4 once and is finished below (R13.4.1).
    rows: list[_Row] = []
    for line in lines:
        scan = scan_time(line.raw)
        row = _Row(line, scan, is_blank(line.raw))
        rows.append(row)
        if row.blank:
            continue
        probe = lp.probe(scan.parts.rest)
        row.probe = probe
        row.dash = probe.spaced_dash
        row.candidate = scan.time is not None or row.dash
        row.bare = bare_candidate(scan.parts.rest, probe.position)  # R13.5
    candidates = 0
    dashes = 0
    for r in rows:
        if not r.candidate:
            continue
        candidates += 1
        if r.dash:
            dashes += 1
    list_context = candidates > 0 and 2 * dashes >= candidates

    # R13.5: confirmed bare positions are removed before parsing. R13.4.1: one parse per line.
    confirmed = confirm_positions([r.bare for r in rows], [r.candidate for r in rows])
    for i, r in enumerate(rows):
        if r.blank or r.probe is None:
            continue
        bare = r.bare if confirmed[i] else None
        probe = lp.probe(bare.stripped) if bare is not None else r.probe
        r.marker = has_marker_word(
            bare.stripped if bare is not None else r.scan.parts.rest, markers
        )
        # R13.4.2: the R6.10 tight dash only on timed lines without a marker word.
        track = probe.finish(list_context and r.scan.time is not None and not r.marker)
        if bare is not None:
            track = replace(track, position=Position(raw=bare.raw, number=bare.number))
        r.track = track
        r.separated = has_separator(track, probe.empty)

    # R13.3
    entries: list[TracklistEntry] = []
    for i, r in enumerate(rows):
        kind = _kind_of(r)
        keep = kind == "track" or kind == "marker"
        entries.append(
            TracklistEntry(
                line=i + 1,
                kind=kind,
                eol=r.line.eol,
                raw=r.line.raw,
                parts=r.scan.parts,
                time=r.scan.time,
                track=r.track if keep else None,
            )
        )

    warnings = tracklist_warnings(
        entries,
        [r.scan.invalid for r in rows],
        # R13.7 line-unparsed: a track entry whose track part has no separator (R13.3.4).
        [e.kind == "track" and not r.separated for e, r in zip(entries, rows)],
        opts.audio_seconds,
    )
    return Tracklist(
        spec_version=SPEC_VERSION,
        entries=tuple(entries),
        warnings=tuple(warnings),
        stats=_stats_of(entries),
    )


def _kind_of(r: _Row) -> EntryKind:
    if r.blank or r.track is None:
        return "blank"
    marker = not r.separated and r.marker
    if r.scan.time is not None:
        return "marker" if marker else "track"
    rest = r.scan.parts.rest
    if is_link(rest):
        return "link"
    if is_header(rest):
        return "header"
    if marker:
        return "marker"
    return "track" if r.separated else "note"


def _stats_of(entries: list[TracklistEntry]) -> Stats:
    tracks = 0
    markers = 0
    timed = 0
    first_time: Optional[float] = None
    last_time: Optional[float] = None
    for e in entries:
        if e.kind == "track":
            tracks += 1
        if e.kind == "marker":
            markers += 1
        if e.time is not None:
            timed += 1
            if first_time is None:
                first_time = e.time.seconds
            last_time = e.time.seconds
    return Stats(len(entries), tracks, markers, timed, first_time, last_time)
