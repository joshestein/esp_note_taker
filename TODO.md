# TODO

Implementation checklist for the voice-memo recording flow. See `CONTEXT.md` for terms and `docs/adr/` for the reasoning behind decisions.

## Recording flow (core)

- [x] Add LED GPIO to `config.h` (Recording Indicator) -- pick a free pin
- [x] Bring in SD support: lift `sdcard_bsp` (SDMMC 1-line, D0=40 CLK=39 CMD=41), mount `/sdcard` at boot, keep mounted in Idle
- [x] Bring in codec support: lift `audio_bsp` (ES8311 via `esp_codec_dev`), configure **mono / 16kHz / 16-bit**
- [x] Power down the codec in Idle **in software** (`esp_codec_dev_close` on Capture stop, `esp_codec_dev_open` on record start) to stop mic-bias drain -- no board pin gates the codec supply (hardware-verified: `Audio_PWR_PIN` off still records). Split `audio_bsp` done: one-time `init` (I2S + I2C bus + codec create) vs per-Capture `record_start`/`record_stop` (open/close). `open`/`close` own the I2S channel enable/disable (redundant manual enable removed). `start`/`stop` called from inside `record_task` so the codec has a single owner (no read-after-close race). See ADR 0002
  - `Audio_PWR_PIN` (GPIO42) gates only the **speaker amp** (PAVCC via Q6), **active-low** (LOW=on): leave off in Idle/record, on only for playback
- [x] Dedicated record task (Model A): `esp_codec_dev_read` -> PSRAM buffer -> `fwrite`
- [ ] PSRAM ring/double buffer between codec read and SD write (absorbs SD write stalls)
- [x] WAV writer: placeholder 44-byte header on open, stream samples counting bytes, patch `RIFF`+`data` sizes on close
- [x] Filename from monotonic sequence (v1): `note_NNNN.wav`, 4-digit zero-pad, assigned at Capture start
  - Source: scan `/sdcard` **once at mount** (boot / card insert) for the highest existing `note_NNNN`, cache max in RAM; each Capture uses `max+1` and bumps the RAM value. No NVS.
  - No per-record scan -> zero added latency on the light-sleep wake-to-record path (SD stays mounted through Idle)
  - Card swap self-heals: remount rescans, so the number is always derived from the card actually present (no cross-card clobber)
  - Discarded (<2s) Capture: number already consumed, leaves a gap -- harmless, sort still holds
  - Ceiling 9999: past it the pad overflows and natural sort breaks -- accepted for v1
- [ ] Deferred: filename from RTC (PCF85063, I2C 0x51) `note_YYYYMMDD_HHMMSS.wav` once clock set via Sync/NTP; needs I2C bus hoisted out of `audio_bsp` so codec + RTC share one bus
- [ ] Record everything naively -- no warm-up detection, no sample discard

## State machine (replace stub in `main.c`)

- [x] Idle -> Recording: LED on, codec on, open WAV, start record task
- [x] Recording -> Finalizing: task signals end via `CAPTURE_ENDED_BIT`; unified exit for user-stop / mid-record error / alloc-fail. See ADR 0004
- [x] Finalizing: **drop** button presses (no queued restart), then -> Idle
- [x] Wire LED off on leaving Recording
- [x] Replace the fake 2s `vTaskDelay` "save" (`main.c:32-36`) with real Finalizing

## Reliability

- [ ] 2-second minimum Capture: discard whole Capture if shorter (measure in bytes, 64,000 = 2s mono). **Mechanic undecided** -- see Open below
- [x] Check every `fwrite`/codec-read return; on failure the task records the outcome (`capture_ok`, default-false, task is sole writer) and breaks -> FINALISING deletes the partial file via `remove(path)`.
- [ ] Error Indication LED pattern on that failure (currently only logs)
- [ ] No SD card at record start: refuse to start, Error Indication, stay Idle
- [ ] Resource auto-stop: stop cleanly on low battery / near-full card (thresholds TBD)
- [ ] Accept sudden-power-loss corruption of the newest file (no periodic header flush)

## Power management

- [ ] Light sleep in Idle (per ADR 0001), GPIO wake on either button
  - No `gpio_hold` plumbing needed for the audio-amp gate: light sleep auto-retains digital GPIO output state, so `Audio_PWR_PIN` (GPIO42) stays off through Idle on its own. (GPIO42 is not RTC-capable -- >21 -- so it couldn't be held or used as a wake source under deep sleep anyway; another point for light sleep. Wake pins GPIO0/GPIO18 are both RTC-capable.)

## Open design decisions

- [ ] **2s-discard mechanic (Q7):** buffer-first (don't open file until 2s buffered) vs write-then-delete (`remove()` on short stop)
- [ ] Auto-stop thresholds: battery %, free-space margin
- [ ] Error Indication LED pattern (blink cadence, distinct from steady Recording Indicator)

## Menu + UI

- [ ] Menu mode (mutually exclusive with recording; Record button repurposed to "act on card")
- [ ] One-card stepping navigation: Menu btn = next card, long-press Menu = exit toward Idle
- [ ] Top-level cards: Recordings, Sync, Storage
- [ ] E-paper rendering: discrete screens, partial refresh per card, periodic full refresh to clear ghosting
- [ ] Battery glyph on Idle screen (read `VBAT_PWR_PIN` GPIO17) -- not a menu card
- [ ] Power-off/sleep gesture: long-press Menu from Idle
- [ ] Recordings card: step Captures newest-first, Record btn = play through speaker (power codec on for playback)
- [ ] Playback path: `esp_codec_dev_write` via `audio_bsp`. Power the speaker amp on first: `Audio_PWR_PIN` (GPIO42) LOW; the codec driver drives PA_CTRL (GPIO46) enable itself. Amp back off after playback
- [ ] Storage card: free space, # Captures, erase-synced action

## Sync (device side; companion is a separate repo)

- [ ] WiFi creds in a **gitignored** header (`wifi_secrets.h`) -- never commit. Hardcode computer address too (prototype)
- [ ] Sync card: manual trigger (Record btn) -- connect WiFi, upload, disconnect. Radio off otherwise
- [ ] Enumerate unsynced Captures (record folder), skip `/sdcard/synced/`
- [ ] HTTP POST each WAV to companion listener; on 200 (file flushed) `rename()` into `/sdcard/synced/`
- [ ] Leave file unsynced on timeout/non-200/drop; retry next sync (idempotent by filename)
- [ ] Download phase: GET transcript list from companion, fetch missing `.txt`, store `/sdcard/transcripts/note_*.txt` (paired by stem)
- [ ] Recordings card: show one-line transcript preview if present (no paging reader -- hold only)
- [ ] NTP time-set during sync -> update RTC (PCF85063)
- [ ] Sync progress on e-paper (upload count + transcript-download count)

## Deferred

- [ ] WiFi provisioning: USB-serial pairing via companion, or SoftAP (replaces hardcoded creds)
- [ ] Automatic/background sync (pending power budget)
- [ ] Bonus: device-hosted LAN download page (no transcription)
- [ ] Manual time-set UI -- likely unnecessary once NTP works
