# Companion

The paired-computer half of the ESP32-S3 voice-memo device. It receives Captures (WAV) uploaded by the device over the LAN, transcribes them locally with Parakeet (`parakeet-mlx`), and serves the transcripts back on a later sync.

## Requirements

- Apple Silicon Mac (`parakeet-mlx` uses MLX)
- [`uv`](https://docs.astral.sh/uv/)
- `ffmpeg` on `PATH`
- Runs on the same LAN as the device

## Setup

```sh
cd companion
uv venv --python 3.12 --python-preference only-managed .venv
uv pip install --python .venv/bin/python -r requirements.txt
source .venv/bin/activate

cp .env.example .env
```

Set all unset tokens in `.env`

## Run

```sh
flask --app app run --host 0.0.0.0 --port 8080
```

- `--host 0.0.0.0` so the device can reach it across the LAN (default binds localhost only).
- First boot downloads the model (~2.5 GB) to `~/.cache/huggingface`.

## What it does

- `POST /captures/<name>.wav` — stores the raw WAV durably (temp → `fsync` → atomic rename), returns `{"stored", "size"}`.
- `GET /transcripts` — lists ready transcript filenames.
- `GET /transcripts/<name>.txt` — returns one transcript (`404` if not ready).
- Every request needs `Authorization: Bearer <COMPANION_TOKEN>` or gets `401`.

Transcription runs on a background worker thread (model loaded on that thread: MLX streams are per-thread), so uploads return immediately and the transcript appears a few seconds later — the device fetches it on a subsequent sync.

A Capture that fails transcription moves to `captures/broken/`, with the error in `<name>.err`. To retry: move the WAV back to `captures/`, delete the `.err`, restart.

## Status

v1 is plaintext HTTP (Tier 1): the bearer token gates who may talk to the Companion, but the token and audio are sniffable by a passive attacker on the LAN — an accepted risk for a manual, seconds-long sync. TLS is a deferred, additive change. Native-app packaging for non-technical friends (not Docker — it breaks LAN mDNS) is likewise deferred.
