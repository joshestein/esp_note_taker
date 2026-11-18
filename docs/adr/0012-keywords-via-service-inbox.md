# Keywords hand off text through service inboxes

A Transcript that starts with a Keyword goes to gtasks-sync or keep-sync. The Companion writes `{"text": ..., "capture": <stem>, ...}` to `~/Library/Application Support/<service>/inbox/<capture stem>.json` and touches `queue/sync-now`. The service turns the file into an op (gtasks ADR 0004, keep-sync ADR 0004). The contract is one JSON file at a known path: no venv paths, no subprocess, no copy of the op format.

The services know nothing about the Companion or Keywords. The Keyword list is in the Companion (`keywords.py`), with any service fields each Keyword adds. An append Keyword names its Keep note by id (`append_to`), not by title: titles are not unique, and they change.

The service makes `inbox/` and `queue/`. The Companion makes no dirs: if `inbox/` does not exist, that service's Keywords are off, and the Companion logs a warning. A failure only logs. It never makes a Broken Capture and is not retried.

## Considered Options

- **`gtasks-sync add` CLI**: the Companion must find another project's venv, and a hung subprocess stops the transcription queue.
- **Import the service as a library**: pulls the service's Google dependencies into the Companion venv.
- **Write op files directly**: a third copy of the op format.
- **Plain-text inbox file**: carries one field, so no `append_to` or capture time.
- **Append target by title or Keep label, resolved by the service**: no ids in the Companion, but a title match guesses wrong, and a label lookup adds service code for one caller.
- **Services publish their Keywords**: no Companion config, but the services would then know about Keywords.
