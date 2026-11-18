"""Hands Keyword text to a service through its inbox (ADR 0012).

The contract is one JSON file at a known path: no import, no subprocess, no
copy of the service's op format. The service makes inbox/ and queue/, never
the Companion.
"""

import json
from pathlib import Path

APP_SUPPORT = Path.home() / "Library" / "Application Support"


def is_set_up(service: str, base: Path = APP_SUPPORT) -> bool:
    """False turns the service's Keywords off."""
    return (base / service / "inbox").is_dir()


def send(service: str, capture: str, fields: dict[str, str], base: Path = APP_SUPPORT) -> bool:
    """Write one inbox file. False if the service is not set up."""
    # Checked per send, so a later service setup needs no Companion restart.
    if not is_set_up(service, base):
        return False
    app_dir = base / service
    # No captured_at: a note_NNNN stem carries no capture time (ADR 0009).
    final = app_dir / "inbox" / f"{capture}.json"
    tmp = final.with_name(final.name + ".part")  # services skip *.json.part
    tmp.write_text(json.dumps({**fields, "capture": capture}), encoding="utf-8")
    tmp.replace(final)
    (app_dir / "queue" / "sync-now").touch()
    return True
