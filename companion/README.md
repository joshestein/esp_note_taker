# Companion

The paired-computer half of the ESP32-S3 voice-memo device. It receives Captures (WAV) uploaded by the device over the LAN, transcribes them locally with Whisper, and serves the transcripts back on a later sync.

## Requirements

- Python 3.12 (pinned in `.python-version`; use `pyenv` or any 3.12 interpreter)
- Runs on the same LAN as the device

## Setup

```sh
cd companion
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt

cp .env.example .env
```

Set all unset tokens in `.env`

## Run

```sh
flask --app app run --host 0.0.0.0 --port 8080
```

- `--host 0.0.0.0` so the device can reach it across the LAN (default binds localhost only).
- First boot downloads the `base.en` model (~150 MB) to `~/.cache/huggingface`, then loads it (~2-4 s) before serving.

## What it does

- `POST /captures/<name>.wav` — stores the raw WAV durably (temp → `fsync` → atomic rename), returns `{"stored", "size"}`.
- `GET /transcripts` — lists ready transcript filenames.
- `GET /transcripts/<name>.txt` — returns one transcript (`404` if not ready).
- Every request needs `Authorization: Bearer <COMPANION_TOKEN>` or gets `401`.

Transcription runs on a background worker thread (one Whisper model, loaded once), so uploads return immediately and the transcript appears a few seconds later — the device fetches it on a subsequent sync.

## Status

v1 is plaintext HTTP (Tier 1): the bearer token gates who may talk to the Companion, but the token and audio are sniffable by a passive attacker on the LAN — an accepted risk for a manual, seconds-long sync. TLS is a deferred, additive change. Native-app packaging for non-technical friends (not Docker — it breaks LAN mDNS) is likewise deferred.
