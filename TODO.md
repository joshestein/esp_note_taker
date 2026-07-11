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
- [ ] Deferred: filename from RTC (PCF85063, I2C 0x51) `note_YYYYMMDD_HHMMSS.wav` once clock set via Sync (Companion `Date` header); needs I2C bus hoisted out of `audio_bsp` so codec + RTC share one bus
- [x] Record everything naively -- no warm-up detection, no sample discard (`record_task` has no discard logic; this is the implemented behavior)

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
- [x] Accept sudden-power-loss corruption of the newest file (no periodic header flush -- non-goal, header patched on close only)

## Power management

- [ ] Light sleep in Idle (per ADR 0001), GPIO wake on either button
  - No `gpio_hold` plumbing needed for the audio-amp gate: light sleep auto-retains digital GPIO output state, so `Audio_PWR_PIN` (GPIO42) stays off through Idle on its own. (GPIO42 is not RTC-capable -- >21 -- so it couldn't be held or used as a wake source under deep sleep anyway; another point for light sleep. Wake pins GPIO0/GPIO18 are both RTC-capable.)

## Open design decisions

- [ ] **2s-discard mechanic (Q7):** buffer-first (don't open file until 2s buffered) vs write-then-delete (`remove()` on short stop)
- [ ] Auto-stop thresholds: battery %, free-space margin
- [ ] Error Indication LED pattern (blink cadence, distinct from steady Recording Indicator)

## Menu + UI

- [x] **Recording Screen** (first e-paper screen; see ADR 0005): `epaper_driver_bsp` lifted from the 1.54 example, LVGL v9 wrapped behind C-API `display_bsp`; hardware-verified. Idle="ready", Recording=circle+"REC", wired best-effort into `main.c`
  - `display_init()` at boot after sdcard/audio init; paint minimal Idle screen with a **full refresh** (establishes partial base, clears ghosting)
  - `display_show_recording()` = filled circle + "recording"; `display_show_idle()` = minimal placeholder (real Idle screen + battery glyph deferred, TODO 57)
  - Idle<->Recording flips use **partial refresh** (~0.3s, no flash)
  - Non-fatal + trailing: never `ESP_ERROR_CHECK` display in the record path; start record task first then paint recording, close WAV first then paint idle
  - No separate Finalizing screen (state is brief; go recording -> idle)
- [ ] Menu mode (mutually exclusive with recording; Record button repurposed to "act on card")
- [ ] One-card stepping navigation: Menu btn = next card, long-press Menu = exit toward Idle
- [ ] Top-level cards: Recordings, Sync, Storage
- [ ] E-paper rendering: discrete screens, partial refresh per card, periodic full refresh to clear ghosting
- [ ] Battery glyph on Idle screen (read `VBAT_PWR_PIN` GPIO17) -- not a menu card
- [x] Power-off/sleep gesture: long-press (~2s) Menu from Idle -> Deep Sleep (`main.c` `MENU_SLEEP_BIT` -> `enter_deep_sleep`)
- [ ] Recordings card: step Captures newest-first, Record btn = play through speaker (power codec on for playback)
- [ ] Playback path: `esp_codec_dev_write` via `audio_bsp`. Power the speaker amp on first: `Audio_PWR_PIN` (GPIO42) LOW; the codec driver drives PA_CTRL (GPIO46) enable itself. Amp back off after playback
- [ ] Storage card: free space, # Captures, erase-synced action

## Companion (`companion/` -- Python + Flask, see ADR 0008)

- [ ] Skeleton scaffold: `companion/` dir, `.python-version` (3.12), `.venv/` gitignored, Flask dep
- [ ] Flask app, 3 endpoints per `docs/sync-protocol.md`: `POST /captures/<name>.wav`, `GET /transcripts`, `GET /transcripts/<name>.txt`
- [ ] Upload handler: stream body to temp path -> fsync -> rename into `captures/`, then respond `{stored,size}` 200 (the 200 must guarantee the file is committed)
- [ ] Stub transcript (no Whisper yet): on upload, immediately write `transcripts/<stem>.txt` so the device download phase is testable end-to-end. This is the seam real transcription replaces
- [ ] Bearer auth: reject missing/wrong `Authorization` with 401 (hardcoded dev token now; move to gitignored `.env` later)
- [ ] Correct `Date` response header (default in most libs) for device RTC-set
- [ ] Deferred: swap stub for real `faster-whisper`, run as a **background worker** (POST returns fast, transcription async)
- [ ] Deferred: native-app packaging (Briefcase) for mixed-OS friends -- not Docker (breaks LAN mDNS, ADR 0008)

## Sync (device side; companion lives in `companion/`)

- [ ] WiFi creds in a **gitignored** header (`wifi_secrets.h`) -- never commit. Also holds the per-owner Companion mDNS hostname (e.g. `josh-memo.local`) and the shared bearer token
- [ ] Companion discovery: resolve per-owner mDNS hostname at sync start (ESP-IDF `mdns`), not a hardcoded IP. Multiple devices on one LAN disambiguated by distinct per-owner hostnames
- [ ] Sync auth: shared bearer token in `Authorization` header on every request; Companion 401s if missing/wrong
- [ ] Deferred (Tier 2): TLS for sync -- encrypt body + verify Companion via pinned cert. Additive over the plaintext token
- [ ] Sync card: manual trigger (Record btn) -- connect WiFi, upload, disconnect. Radio off otherwise
- [ ] Sync session envelope: `wifi connect -> mDNS resolve -> [uploads then downloads] -> wifi stop`. **Bounded timeouts** (connect ~10s, mDNS ~3s); any failure -> radio off, Error Indication, back to Menu (no hang)
- [ ] Single keep-alive `esp_http_client` reused across all requests (one TCP setup, HTTP/1.1 keep-alive) -- do not reconnect per file
- [ ] Order: upload all unsynced **oldest-first** (oldest memos safest if battery dies mid-sync), then download transcripts
- [ ] Sync is **not atomic**: a resumable sequence of independent per-file commits (per-file 200 gates its own `rename`). Interrupted sync resumes next time; idempotent by filename, no rollback
- [ ] Enumerate unsynced Captures (record folder), skip `/sdcard/synced/`
- [ ] Upload wire format: raw-body `POST /captures/note_NNNN.wav` (filename in URL path), `Authorization` + `Content-Length` headers, WAV bytes as raw body (no multipart). **Stream from SD**: `stat` size -> `esp_http_client_open(client, size)` -> loop `fread` ~4KB chunk -> `esp_http_client_write` -> EOF. Flat RAM regardless of memo length; no chunked transfer-encoding
- [ ] On 200 (file flushed/fsync'd, companion returns `{stored,size}`) `rename()` into `/sdcard/synced/`
- [ ] Leave file unsynced on timeout/non-200/drop; retry next sync (idempotent by filename)
- [ ] Download phase: `GET /transcripts` returns a bare JSON array of ready stems (`["note_0003","note_0005"]`); ESP diffs against local `/sdcard/transcripts/` and `GET /transcripts/note_NNNN.txt` for each missing stem. **Server stateless about the device** -- ESP never sends its "have" list; diff is local. `.txt` only (companion keeps the `.json` segments file)
- [ ] Store fetched transcripts at `/sdcard/transcripts/note_*.txt` (paired by stem). **Atomic write** (v1): stream body to `note_NNNN.txt.part`, `rename()` to `.txt` only after a clean 200-to-EOF. A dropped GET leaves a `.part` (diff ignores it -> re-fetched next sync), never a truncated `.txt` that the presence-diff would treat as "have." Clean up stray `.part` at sync start
- [ ] Recordings card: show one-line transcript preview if present (no paging reader -- hold only)
- [ ] Time-set during sync: parse the Companion's HTTP `Date` response header (no SNTP/internet) -> set RTC (PCF85063). **Deferred** with the rest of timestamp naming; v1 stays sequence-based. TZ offset (for local-time filenames) decided later -- likely hardcoded in `wifi_secrets.h`
- [ ] Dedicated **sync task** (mirror `record_task`): owns connect->transfer->disconnect, signals completion via an event-group bit. HTTP/WiFi block for seconds -- must not run on the button/LVGL task
- [ ] Sync progress on e-paper: **phase-level only** for v1 ("Connecting" / "Uploading" / "Downloading" / final "Synced N up, M down"). ~4 partial refreshes total -- avoids per-file refresh thrash + ghosting (partial refresh ~0.3s each)
- [ ] Deferred: throttled live counter (`7/20`, repaint every Nth file or ~2s) instead of phase-level only
- [ ] No sync cancel in v1 (sync is seconds; let it finish)
- [ ] Deferred: between-file cancel -- press Menu sets a flag, current file finishes its commit, loop bails clean to Menu (never mid-`POST`); already-synced files stay synced

## Deferred

- [ ] WiFi provisioning: USB-serial pairing via companion, or SoftAP (replaces hardcoded creds)
- [ ] Automatic/background sync (pending power budget)
- [ ] Bonus: device-hosted LAN download page (no transcription)
- [ ] Manual time-set UI -- likely unnecessary once sync time-set (Companion `Date` header) works
