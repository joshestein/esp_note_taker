"""Keyword parsing: a Transcript that starts with a Keyword becomes a task (ADR 0012).

No I/O and no model, so tests can import this without loading app.py.
"""

import re

# Hesitation sounds the transcriber keeps ("Uh", "Um,", "Hmm..."). They carry no
# meaning anywhere, so they go from the whole text, with their trailing
# punctuation.
_DISFLUENCY = re.compile(
    r"\b(?:u+h+|u+m+|uhm+|e+r+m+|e+r+|a+h+|h+m+|m+m+)\b[,.…!?]*\s*", re.IGNORECASE
)

# Discourse words that only mean nothing at the start ("So, to do ..."). In the
# middle they can carry meaning ("I like eggs"), so they go from the start only.
_LEADING = re.compile(
    r"^(?:(?:so|okay|ok|oh|well|like|right|yeah|alright|all right|and|also)\b[\s,.…!?\-–—]*)+",
    re.IGNORECASE,
)

# Start of text, the Keyword ("to do", "todo", "to-do", any case), then a word
# boundary: "Today", "To-dos" and "To double-check" do not match.
# After the Keyword, remove punctuation and spaces before the title. A character
# not in this set stays in the title, which is safe: the task still gets made.
_TODO = re.compile(r"to[\s-]?do\b[\s,.:;!?…\-–—]*", re.IGNORECASE)


def strip_fillers(text: str) -> str:
    """Text without hesitation sounds anywhere, or discourse words at the start."""
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
    # Strip trailing punctuation, but not an ellipsis
    if title.endswith(".") and not title.endswith(".."):
        title = title[:-1].rstrip()
    return title or None
