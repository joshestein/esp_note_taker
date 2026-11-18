"""Hands a Keyword task to gtasks-sync through its inbox (ADR 0012, gtasks ADR 0004).

The contract is one JSON file at a known path: no import, no subprocess, no
copy of the service's op format.
"""

import json
from pathlib import Path

GTASKS_DIR = Path.home() / "Library" / "Application Support" / "gtasks-sync"


def is_set_up(app_dir: Path = GTASKS_DIR) -> bool:
    """False turns the Keyword off. The service makes inbox/ and queue/, never the Companion."""
    return (app_dir / "inbox").is_dir()


def send_task(capture: str, title: str, app_dir: Path = GTASKS_DIR) -> bool:
    """Queue one task. False if gtasks-sync is not set up."""
    # Checked per send, so a later `gtasks-sync setup` needs no Companion restart.
    if not is_set_up(app_dir):
        return False
    inbox = app_dir / "inbox"
    queue = app_dir / "queue"
    # No captured_at: a note_NNNN stem carries no capture time (ADR 0009).
    final = inbox / f"{capture}.json"
    tmp = final.with_name(final.name + ".part")  # the service skips *.json.part
    tmp.write_text(json.dumps({"text": title, "capture": capture}), encoding="utf-8")
    tmp.replace(final)
    (queue / "sync-now").touch()
    return True
