"""Hands a Keyword task to gtasks-sync through its inbox (ADR 0012, gtasks ADR 0004).

The contract is one JSON file at a known path: no import of the service, no
subprocess, no copy of its op format.
"""

import json
from pathlib import Path

GTASKS_DIR = Path.home() / "Library" / "Application Support" / "gtasks-sync"


def send_task(capture: str, title: str, app_dir: Path = GTASKS_DIR) -> bool:
    """Queue one task. False if gtasks-sync is not set up, so the Keyword is off."""
    inbox = app_dir / "inbox"
    if not inbox.is_dir():
        return False
    # No captured_at yet: a note_NNNN stem carries no capture time (ADR 0009).
    final = inbox / f"{capture}.json"
    tmp = final.with_name(final.name + ".part")  # the service skips *.json.part
    tmp.write_text(json.dumps({"text": title, "capture": capture}), encoding="utf-8")
    tmp.replace(final)
    (app_dir / "queue" / "sync-now").touch()
    return True
