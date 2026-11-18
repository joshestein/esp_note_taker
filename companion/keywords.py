"""Keyword parsing: a Transcript that starts with a Keyword goes to another app (ADR 0012).

No I/O and no model, so tests can import this without loading app.py.
"""

import re
from dataclasses import dataclass, field

# Hesitation sounds ("Uh", "Um,", "Hmm..."): meaningless anywhere, so removed
# everywhere, with their trailing punctuation.
_DISFLUENCY = re.compile(
    r"\b(?:u+h+|u+m+|uhm+|e+r+m+|e+r+|a+h+|h+m+|m+m+)\b[,.…!?]*\s*", re.IGNORECASE
)

# Discourse words: meaningless only at the start ("So, to do ..."), not in the
# middle ("I like eggs").
_LEADING = re.compile(
    r"^(?:(?:so|okay|ok|oh|well|like|right|yeah|alright|all right|and|also)\b[\s,.…!?\-–—]*)+",
    re.IGNORECASE,
)

# After the Keyword: \b rejects "Today", "To-dos", "Notes" and "Projected", then
# the punctuation before the text is removed.
_AFTER = r"\b[\s,.:;!?…\-–—]*"

# The "Projects #programming" checklist. By id, so a rename does not break it.
PROJECTS_NOTE = "180fa389fa9.b34e92fae734d2f3"


@dataclass(frozen=True)
class Keyword:
    name: str
    words: str  # regex for the spoken Keyword
    service: str  # the service's data dir, under Application Support
    fields: dict[str, str] = field(default_factory=dict)  # added to the inbox file
    strip_period: bool = False  # the text is a title or a list item
    pattern: re.Pattern[str] = field(init=False, repr=False, compare=False)

    def __post_init__(self):
        object.__setattr__(self, "pattern", re.compile(self.words + _AFTER, re.IGNORECASE))


KEYWORDS = [
    Keyword("to do", r"to[\s-]?do", "gtasks-sync", strip_period=True),
    Keyword("note", r"note", "keep-sync"),
    Keyword("project", r"projects?","keep-sync", {"append_to": PROJECTS_NOTE}, strip_period=True),
]


@dataclass(frozen=True)
class Match:
    keyword: Keyword
    text: str


def strip_fillers(text: str) -> str:
    text = _DISFLUENCY.sub("", text)
    text = re.sub(r"\s+([,.;:!?…])", r"\1", text)  # "buy , milk" -> "buy, milk"
    return _LEADING.sub("", text.strip()).strip()


def parse(text: str) -> Match | None:
    """The Keyword and the text after it, or None if the Transcript has no Keyword."""
    text = strip_fillers(text)
    for keyword in KEYWORDS:
        if (m := keyword.pattern.match(text)) is None:
            continue
        rest = text[m.end() :].strip()
        if keyword.strip_period and rest.endswith(".") and not rest.endswith(".."):
            rest = rest[:-1].rstrip()  # keep an ellipsis
        return Match(keyword, rest) if rest else None
    return None
