"""Keyword parsing: a Transcript that starts with a Keyword becomes a task (ADR 0012).

No I/O and no model, so tests can import this without loading app.py.
"""

import re

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

# The Keyword ("to do", "todo", "to-do"), then the punctuation before the title.
# \b rejects "Today", "To-dos" and "To double-check".
_TODO = re.compile(r"to[\s-]?do\b[\s,.:;!?…\-–—]*", re.IGNORECASE)


def strip_fillers(text: str) -> str:
    text = _DISFLUENCY.sub("", text)
    text = re.sub(r"\s+([,.;:!?…])", r"\1", text)  # "buy , milk" -> "buy, milk"
    return _LEADING.sub("", text.strip()).strip()


def parse_todo(text: str) -> str | None:
    """The task title if text starts with the to-do Keyword, else None."""
    text = strip_fillers(text)
    match = _TODO.match(text)
    if match is None:
        return None
    title = text[match.end() :].strip()
    if title.endswith(".") and not title.endswith(".."):  # keep an ellipsis
        title = title[:-1].rstrip()
    return title or None
