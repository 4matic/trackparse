"""Vocabulary tables compiled from spec/data, optionally extended by ``keywords`` (R12)."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any, Optional

from ._data import (
    DESCRIPTORS_DATA,
    EXTENSIONS_DATA,
    FEAT_MARKERS_DATA,
    GENRES_DATA,
    JOINERS_DATA,
    JUNK_DATA,
    NO_SPLIT_BEFORE_DATA,
    PLATFORM_SUFFIXES_DATA,
    STOPWORDS_DATA,
    UNKNOWN_TOKENS_DATA,
    VERSION_KEYWORDS_DATA,
)
from ._words import PhraseTable, ascii_lower, js_trim


class SpacedJoiner:
    __slots__ = ("canonical", "case_sensitive", "is_and", "raw")

    def __init__(self, raw: str, canonical: str, is_and: bool, case_sensitive: bool) -> None:
        self.raw = raw
        self.canonical = canonical
        self.is_and = is_and
        self.case_sensitive = case_sensitive


class RunInfo:
    """Category of a word in the modifier run left of a version head (R5.7.2)."""

    __slots__ = ("descriptor", "genre", "head", "type_capable")

    def __init__(
        self,
        type_capable: Optional[str] = None,
        descriptor: Optional[str] = None,
        genre: bool = False,
        head: Optional[str] = None,
    ) -> None:
        self.type_capable = type_capable
        self.descriptor = descriptor
        self.genre = genre
        self.head = head


class Tables:
    __slots__ = (
        "bracket_only_feat",
        "extensions",
        "feat_canonical",
        "feat_markers",
        "generic_head_types",
        "heads",
        "junk_connectors",
        "junk_or_genre",
        "junk_phrases",
        "label_suffixes",
        "no_split_before",
        "platform_suffixes",
        "prefix_forms",
        "producer_markers",
        "run_words",
        "spaced_joiners",
        "stopwords",
        "tight_joiners",
        "title_feat_markers",
        "title_producer_markers",
        "unknown_case_insensitive",
        "unknown_case_sensitive",
    )

    feat_markers: PhraseTable[bool]
    bracket_only_feat: PhraseTable[bool]
    producer_markers: PhraseTable[bool]
    #: R8.3: the only unbracketed feat markers on the title side (+ keywords feat_markers).
    title_feat_markers: PhraseTable[bool]
    #: R8.3: the only unbracketed producer markers on the title side.
    title_producer_markers: PhraseTable[bool]
    #: Junk phrases plus genres (kind ``genre``): the R5.1 tokenizer vocabulary.
    junk_or_genre: PhraseTable[str]
    #: Junk phrases only (R8.4).
    junk_phrases: PhraseTable[str]
    junk_connectors: frozenset[str]
    label_suffixes: frozenset[str]
    heads: PhraseTable[str]
    generic_head_types: frozenset[str]
    #: Everything allowed in the R5.7.2 modifier run, keyed by phrase.
    run_words: PhraseTable[RunInfo]
    prefix_forms: PhraseTable[str]
    spaced_joiners: list[SpacedJoiner]
    tight_joiners: dict[str, str]
    feat_canonical: str
    no_split_before: frozenset[str]
    stopwords: frozenset[str]
    unknown_case_sensitive: frozenset[str]
    unknown_case_insensitive: frozenset[str]
    extensions: frozenset[str]
    platform_suffixes: list[str]


def _is_array_index(key: str) -> bool:
    if not key or len(key) > 10 or not all("0" <= c <= "9" for c in key):
        return False
    if len(key) > 1 and key[0] == "0":
        return False
    return int(key) <= 0xFFFFFFFE


def js_entries(d: Mapping[str, Any]) -> list[tuple[str, Any]]:
    """``Object.entries`` order: integer-like keys first (ascending), then insertion order."""
    items = list(d.items())
    ints = sorted((kv for kv in items if _is_array_index(kv[0])), key=lambda kv: int(kv[0]))
    return ints + [kv for kv in items if not _is_array_index(kv[0])]


def _str_list(value: Any) -> list[str]:
    if not isinstance(value, (list, tuple)):
        return []
    return [s for s in value if isinstance(s, str)]


def _str_map(value: Any) -> list[tuple[str, str]]:
    if not isinstance(value, Mapping):
        return []
    return [(k, v) for k, v in js_entries(value) if isinstance(k, str) and isinstance(v, str)]


def _lower_keys(value: Any) -> list[str]:
    return [k for k in (js_trim(ascii_lower(s)) for s in _str_list(value)) if len(k) > 0]


def build_tables(keywords: Optional[Mapping[str, Any]] = None) -> Tables:
    kw: Mapping[str, Any] = keywords if isinstance(keywords, Mapping) else {}
    t = Tables()
    extra_feat = _lower_keys(kw.get("feat_markers"))
    t.feat_markers = PhraseTable((m, True) for m in [*FEAT_MARKERS_DATA["markers"], *extra_feat])
    t.bracket_only_feat = PhraseTable((m, True) for m in FEAT_MARKERS_DATA["bracketOnly"])
    t.producer_markers = PhraseTable((m, True) for m in FEAT_MARKERS_DATA["producer"])
    t.title_feat_markers = PhraseTable(
        (m, True) for m in [*FEAT_MARKERS_DATA["titleMarkers"], *extra_feat]
    )
    t.title_producer_markers = PhraseTable((m, True) for m in FEAT_MARKERS_DATA["titleProducer"])

    junk_entries: list[tuple[str, str]] = [
        *js_entries(JUNK_DATA["phrases"]),
        *_str_map(kw.get("junk")),
    ]
    genres: list[str] = [*GENRES_DATA["genres"], *_lower_keys(kw.get("genres"))]
    t.junk_phrases = PhraseTable(junk_entries)
    # Junk phrases are added first, so they win over an identical genre key.
    t.junk_or_genre = PhraseTable([*junk_entries, *((g, "genre") for g in genres)])

    head_entries: list[tuple[str, str]] = [
        *js_entries(VERSION_KEYWORDS_DATA["heads"]),
        *_str_map(kw.get("version_heads")),
    ]
    t.heads = PhraseTable(head_entries)

    run_info: dict[str, RunInfo] = {}

    def note(key: str, **patch: Any) -> None:
        k = js_trim(ascii_lower(key))
        info = run_info.get(k)
        if info is None:
            info = RunInfo()
        merged = RunInfo(info.type_capable, info.descriptor, info.genre, info.head)
        for name, value in patch.items():
            setattr(merged, name, value)
        run_info[k] = merged

    for k, typ in js_entries(VERSION_KEYWORDS_DATA["typeCapableModifiers"]):
        note(k, type_capable=typ)
    for d in [*DESCRIPTORS_DATA["descriptors"], *_lower_keys(kw.get("descriptors"))]:
        note(d, descriptor=js_trim(ascii_lower(d)))
    for g in genres:
        note(g, genre=True)
    for k, typ in head_entries:
        note(k, head=typ)

    t.junk_connectors = frozenset(JUNK_DATA["connectors"])
    t.label_suffixes = frozenset(JUNK_DATA["labelSuffixes"])
    t.generic_head_types = frozenset(VERSION_KEYWORDS_DATA["genericHeads"])
    t.run_words = PhraseTable(run_info.items())
    t.prefix_forms = PhraseTable(js_entries(VERSION_KEYWORDS_DATA["prefixForms"]))
    t.spaced_joiners = [
        SpacedJoiner(
            j["raw"],
            j["canonical"],
            j.get("kind") == "and",
            j.get("caseSensitive") is True,
        )
        for j in JOINERS_DATA["spaced"]
    ]
    t.tight_joiners = {j["raw"]: j["canonical"] for j in JOINERS_DATA["tight"]}
    t.feat_canonical = JOINERS_DATA["featCanonical"]
    t.no_split_before = frozenset(NO_SPLIT_BEFORE_DATA["words"])
    t.stopwords = frozenset(STOPWORDS_DATA["words"])
    t.unknown_case_sensitive = frozenset(UNKNOWN_TOKENS_DATA["caseSensitive"])
    t.unknown_case_insensitive = frozenset(UNKNOWN_TOKENS_DATA["caseInsensitive"])
    t.extensions = frozenset(EXTENSIONS_DATA["extensions"])
    # JS sorts by UTF-16 length; the suffixes are ASCII, so code-point length is the same.
    t.platform_suffixes = sorted(PLATFORM_SUFFIXES_DATA["suffixes"], key=lambda s: -len(s))
    return t


_default_tables: Optional[Tables] = None


def get_default_tables() -> Tables:
    global _default_tables
    if _default_tables is None:
        _default_tables = build_tables()
    return _default_tables


def tables_for(keywords: Optional[Mapping[str, Any]]) -> Tables:
    return build_tables(keywords) if keywords else get_default_tables()


def is_unknown_token(text: str, t: Tables) -> bool:
    """R7.5 / R8.6: an unknown placeholder name (``ID``, ``???``, ``Untitled``…)."""
    return text in t.unknown_case_sensitive or ascii_lower(text) in t.unknown_case_insensitive
