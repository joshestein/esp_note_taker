"""Keyword parsing: a Transcript that starts with a Keyword becomes a task (ADR 0012).

No I/O and no model, so tests can import this without loading app.py.
"""

import re

# Start of text, the Keyword ("to do", "todo", "to-do", any case), then a word
# boundary: "Today", "To-dos" and "To double-check" do not match.
# After the Keyword, remove punctuation and spaces before the title. A character
# not in this set stays in the title, which is safe: the task still gets made.
_TODO = re.compile(r"\s*to[\s-]?do\b[\s,.:;!?…\-–—]*", re.IGNORECASE)


def parse_todo(text: str) -> str | None:
    """The task title if text starts with the to-do Keyword, else None."""
    match = _TODO.match(text)
    if match is None:
        return None
    title = text[match.end():].strip()
    return title or None
