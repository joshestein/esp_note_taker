# TODO

Implementation checklist for the voice-memo recording flow. See `CONTEXT.md` for terms and `docs/adr/` for the reasoning behind decisions.

## Recording flow (core)

- [x] Add LED GPIO to `config.h` (Recording Indicator) -- pick a free pin
- [x] Bring in SD support: lift `sdcard_bsp` (SDMMC 1-line, D0=40 CLK=39 CMD=41), mount `/sdcard` at boot, keep mounted in Idle
- [x] Bring in codec support: lift `audio_bsp` (ES8311 via `esp_codec_dev`), configure **mono / 16kHz / 16-bit**
- [ ] Confirm ADC high-pass filter (REG1B/1C) is enabled on the record path
- [ ] Gate codec power via `Audio_PWR_PIN` (GPIO42): off in Idle, on at record start
- [x] Dedicated record task (Model A): `esp_codec_dev_read` -> PSRAM buffer -> `fwrite`
- [ ] PSRAM ring/double buffer between codec read and SD write (absorbs SD write stalls)
- [x] WAV writer: placeholder 44-byte header on open, stream samples counting bytes, patch `RIFF`+`data` sizes on close
- [ ] Filename from RTC (PCF85063, I2C 0x51): `note_YYYYMMDD_HHMMSS.wav`
- [ ] Record everything naively -- no warm-up detection, no sample discard (startup pop accepted)

## State machine (replace stub in `main.c`)

- [x] Idle -> Recording: LED on, codec on, open WAV, start record task
- [ ] Recording -> Finalizing: signal record task to patch header + close file
- [ ] Finalizing: **drop** button presses (no queued restart), then -> Idle
- [x] Wire LED off on leaving Recording
- [x] Replace the fake 2s `vTaskDelay` "save" (`main.c:32-36`) with real Finalizing

## Reliability

- [ ] 2-second minimum Capture: discard whole Capture if shorter (measure in bytes, 64,000 = 2s mono). **Mechanic undecided** -- see Open below
- [ ] Check every `fwrite` return; on failure abort to Finalizing/Idle + raise Error Indication
- [ ] No SD card at record start: refuse to start, Error Indication, stay Idle
- [ ] Resource auto-stop: stop cleanly on low battery / near-full card (thresholds TBD)
- [ ] Accept sudden-power-loss corruption of the newest file (no periodic header flush)

## Power management

- [ ] Light sleep in Idle (per ADR 0001), GPIO wake on either button

## Open design decisions

- [ ] **2s-discard mechanic (Q7):** buffer-first (don't open file until 2s buffered) vs write-then-delete (`remove()` on short stop)
- [ ] Auto-stop thresholds: battery %, free-space margin
- [ ] Error Indication LED pattern (blink cadence, distinct from steady Recording Indicator)
- [ ] Warm-up: confirm the startup pop is acceptable on real hardware once measured

## Menu + UI

- [ ] Menu mode (mutually exclusive with recording; Record button repurposed to "act on card")
- [ ] One-card stepping navigation: Menu btn = next card, long-press Menu = exit toward Idle
- [ ] Top-level cards: Recordings, Sync, Storage
- [ ] E-paper rendering: discrete screens, partial refresh per card, periodic full refresh to clear ghosting
- [ ] Battery glyph on Idle screen (read `VBAT_PWR_PIN` GPIO17) -- not a menu card
- [ ] Power-off/sleep gesture: long-press Menu from Idle
- [ ] Recordings card: step Captures newest-first, Record btn = play through speaker (power codec on for playback)
- [ ] Playback path: `esp_codec_dev_write` via `audio_bsp`, PA GPIO46
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
