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
- [x] Dropped: making `display_init` fail soft. The display is **fatal**, so the driver ctor's `assert(buffer)` on SPIRAM alloc failure is now consistent, not a violation. A blind device cannot reach the Menu, a Sync, or Idle; limping on just hides that (ADR 0005)

## Power management

- [ ] Light sleep in Idle (per ADR 0001), GPIO wake on either button
  - No `gpio_hold` plumbing needed for the audio-amp gate: light sleep auto-retains digital GPIO output state, so `Audio_PWR_PIN` (GPIO42) stays off through Idle on its own. (GPIO42 is not RTC-capable -- >21 -- so it couldn't be held or used as a wake source under deep sleep anyway; another point for light sleep. Wake pins GPIO0/GPIO18 are both RTC-capable.)

## Open design decisions

- [ ] **2s-discard mechanic (Q7):** buffer-first (don't open file until 2s buffered) vs write-then-delete (`remove()` on short stop)
- [ ] Auto-stop thresholds: battery %, free-space margin
- [ ] Error Indication LED pattern (blink cadence, distinct from steady Recording Indicator)

## Menu + UI

- [x] **Recording Screen** (first e-paper screen; see ADR 0005): `epaper_driver_bsp` lifted from the 1.54 example, LVGL v9 wrapped behind C-API `display_bsp`; hardware-verified. Idle="ready", Recording=circle+"REC", wired into `main.c`
  - `display_init()` at boot after sdcard/audio init; paint minimal Idle screen with a **full refresh** (establishes partial base, clears ghosting)
  - `display_show_recording()` = filled circle + "recording"; `display_show_idle()` = minimal placeholder (real Idle screen + battery glyph deferred, TODO 57)
  - Idle<->Recording flips use **partial refresh** (~0.3s, no flash)
  - **Fatal, but trailing:** `display_init()` is `ESP_ERROR_CHECK`ed like every other init -- the whole UI lives on the panel, so a blind device is a dead device. It stays *ordered* after `start_capture()` (its bring-up is slow, and on a Record-Button wake that time is audio off the front of the memo), so a panel failure does kill an already-running Capture. Accepted. Paints still trail the audio action -- record task starts, then paint recording; WAV closes, then paint idle -- now for latency, not failure isolation
  - No separate Finalizing screen (state is brief; go recording -> idle)
- [x] `menu` component: owns the card ring, index, wrap, timeout, and the `display_bsp` calls. **Reports intents, performs no side effects** (`MENU_INTENT_NONE|SYNC|SLEEP|EXIT`); `main.c` executes them and stays the only state machine. No dependency on `esp_sleep` / `sdcard_bsp` / the sync task
- [x] `MENU` state in `main.c`'s `app_state_t`
- [x] Cards: **Sync, Sleep**. Render both at once; Selection filled, other outlined. Plus a static "hold -> exit" hint
- [x] Menu btn steps the Selection, wrapping: `(card + 1) % CARD_COUNT`. `menu_enter()` always resets it to 0 (Sync)
- [x] **Menu timeout, ~30s** -> auto-exit to Idle. Timer sets a new `MENU_TIMEOUT_BIT` in the existing `button_group`, so `main.c`'s `xEventGroupWaitBits` loop stays the only place anything happens (no new task). Suspend during Syncing
- [x] **Delete the 2s park hold** (ADR 0007 amendment): drop `MENU_SLEEP_BIT`, `MENU_SLEEP_HOLD_MS`, its `iot_button` callback, and the `main.c` branch. Leaves one long-press threshold (1s = exit Menu), and fixes the latent collision where a 2s hold *in* the Menu exits at 1s then parks at 2s
- [x] Sleep card: Card action -> `enter_deep_sleep()`
- [x] `display_bsp`: **no full-refresh path exists after boot** -- `flush_cb` ends in `EPD_DisplayPart()` unconditionally, and `EPD_DisplayPartBaseImage()` runs once in `display_init`. Add a hook so Menu exit can repaint Idle with a full refresh. Partial per step within the Menu
- [ ] Battery glyph on Idle screen (read `VBAT_PWR_PIN` GPIO17) -- not a menu card
- [ ] Deferred: **Storage card** -- free space + # Captures, read-only. Needs new `sdcard_bsp` surface (`esp_vfs_fat_info` for free bytes; a real file count -- `sdcard_scan_max()` is the highest `note_NNNN`, not a count, and drifts on every discarded short Capture). Then `main.c` gathers the values and passes them into `menu_enter()`, so `menu` never depends on `sdcard_bsp`. **Decide first:** does the count mean *all* Captures (snapshot stays valid across a sync) or *unsynced* ones (sync mutates it, so Storage needs a re-render path)?
- [ ] Deferred: **Recordings card** -- blocked on the playback path. First card needing a second level (its action drills into the Capture list, where Menu btn steps Captures and long-press pops a level instead of exiting)
- [ ] Rejected: a Record card. Record btn from Idle already captures in one press
- [ ] Playback path: `esp_codec_dev_write` via `audio_bsp`. Power the speaker amp on first: `Audio_PWR_PIN` (GPIO42) LOW; the codec driver drives PA_CTRL (GPIO46) enable itself. Amp back off after playback
- [ ] Storage card: free space, # Captures, erase-synced action

## Companion (`companion/` -- Python + Flask, see ADR 0008)

- [x] Skeleton scaffold: `companion/` dir + `.venv` (pyenv 3.12) + `requirements.txt`. Remaining: `.python-version` (3.12), `.gitignore` (`.venv/`, `captures/`, `transcripts/`, `__pycache__/`)
- [x] Flask app, 3 endpoints per `docs/sync-protocol.md`: `POST /captures/<name>.wav`, `GET /transcripts`, `GET /transcripts/<name>.txt`
- [x] Upload handler: stream body to `.part` -> `flush`+`fsync` -> `replace` into `captures/`, respond `{stored,size}` 200; failure branch unlinks `.part` + 500
- [x] Real `faster-whisper` transcription (`base.en`, cpu/int8), single background worker thread + `queue.Queue`, atomic `.txt` write. **State-driven recovery:** `enqueue_pending()` rescans `captures/` at startup for WAVs lacking a `.txt`. Verified end-to-end 2026-07-11 (round-trip, 404, idempotent re-upload, rescan recovery). Superseded the instant stub
- [x] Correct `Date` response header (Werkzeug default) for device RTC-set
- [x] Bearer auth: `before_request` gate, 401 empty-body on missing/wrong `Authorization`, constant-time compare. Token from `COMPANION_TOKEN` env (dev default). Verified 2026-07-11. Later: move token to gitignored `.env`
- [ ] Deferred: native-app packaging (Briefcase) for mixed-OS friends -- not Docker (breaks LAN mDNS, ADR 0008)

## Sync (device side; companion lives in `companion/`)

Implements the client side of `docs/sync-protocol.md` (reasoning in ADR 0003 / 0006). The wire behavior *is* the contract; tasks below are device plumbing, not a restatement of it.

- [x] `wifi_secrets.h` (**gitignored**, never commit): Wi-Fi creds + per-owner Companion mDNS hostname (e.g. `josh-memo.local`) + port + shared bearer token. Ship a committed `wifi_secrets.example.h` so a missing file fails loudly at compile time
- [x] Path constants into `config.h` (`SD_MOUNT_POINT`, `SYNCED_DIR`, `TRANSCRIPTS_DIR`, `NOTE_FILENAME_FMT`) -- `main.c` and `sync` must not each hardcode `/sdcard/...`; a disagreement means uploads that never advance
- [x] `mkdir()` `/sdcard/synced/` and `/sdcard/transcripts/` (ignore `EEXIST`). **On a fresh card they don't exist**, so every `rename()` fails, every Capture stays unsynced, and the device re-uploads everything on every sync while reporting success
- [x] `sync` component: does its **own POSIX I/O** on the mounted VFS (`opendir`/`fopen`/`rename`) -- `/sdcard` is a filesystem once mounted, so no `REQUIRES sdcard_bsp`. And no `REQUIRES display_bsp`: it reports, `main.c` paints (mirrors `menu` and `record_task`)
- [x] Dedicated **sync task** (mirror `record_task`): owns connect->transfer->disconnect. Writes phase + counts into a shared struct and signals `SYNC_PROGRESS_BIT` / `SYNC_ENDED_BIT` in the existing `button_group`; `main.c` wakes, reads, paints
- [x] **Wi-Fi init is lazy** -- `nvs_flash_init` / `esp_netif` / event loop / `esp_wifi_init` all inside the sync task, torn down at the end. **Not at boot:** `main.c` starts a Capture *before* `display_init()` on a Record-Button wake from Deep Sleep, so anything added to boot is audio lost off the front of every memo, to save a second on a rare manual sync
- [x] Session envelope: `wifi connect -> mDNS resolve -> uploads (oldest-first) -> downloads -> wifi stop`. Bounded timeouts (connect ~10s, mDNS ~3s); no hang
  - Radio teardown needs `explicit_stop_requested`: `esp_wifi_stop()` raises `STA_DISCONNECTED` exactly like a dropped link, so without the flag the handler reconnects into the stop it is in the middle of performing
- [x] **Per-file failure = skip, not abort.** A timeout/500/dropped connection on one file leaves it unsynced (retried next sync) and the session continues. Aborting would make one unsendable Capture a **poison pill**: enumerated oldest-first, it would abort every future sync and silently block everything behind it
- [x] **Session-level failure = abort** (Wi-Fi won't connect, mDNS won't resolve, 401). Radio off, Sync result. 401 gets its own message ("unauthorized") -- it is permanent until reflash, not transient, and a generic "failed" sends you rebooting the router. (Error Indication LED still open, see Reliability)
- [x] Single keep-alive `esp_http_client` reused across all requests (no per-file reconnect)
- [x] Upload: enumerate unsynced Captures (top-level `*.wav`, skip `/synced/`), stream each from SD per the contract; on 200 `rename()` into `/sdcard/synced/`
  - **Unsynced is a location, not a flag:** the `*.wav` still at the top level *are* the unsynced ones, since a 200 moves them. No index, no state file, nothing that can drift from reality
  - `sdcard_scan_max()` must scan `/synced/` too, or the counter collapses after a sync and the next Capture reuses a taken number -- which, the protocol being idempotent by filename, would silently overwrite a different memo on the Companion
- [x] Download: `GET /transcripts`, diff against local `/sdcard/transcripts/`, fetch each missing stem per the contract (atomic `.part` -> `.txt`); clean up stray `.part` at sync start
  - Hand-parsed (quote-pair scan), no `cJSON`: names are `note_NNNN.txt`, so there are no escapes, commas, or unicode to handle
  - Names must be copied **out** of the shared `chunk` buffer before any fetch -- `download_transcript` reads bodies into `chunk`, so lazy parsing had each transcript overwrite the list still being walked
  - A 404 is not a failure: it means Whisper has not finished that one yet. It stays on the Companion and is picked up next sync
- [ ] Recordings card: show one-line transcript preview if present (hold only, no reader)
- [x] Sync progress on e-paper: **phase-level only** -- "Connecting"/"Uploading"/"Downloading" as partial refreshes, then the **Sync result** as a full refresh (the screen that persists is the one that should be clean, and it's where the Menu's accumulated ghosting gets cleared)
- [x] Sync **leaves the Menu on the way in** and lands in **Idle** on the way out, showing the Sync result. The result does not expire -- the next button press repaints. `menu_exit()` no longer paints Idle itself; `main.c` paints whatever comes next
- [x] Result must show **failures, not just successes** ("3 up, 1 down, 2 failed"). A partial sync that reads like a complete one is the failure the screen exists to prevent
- [ ] Allow companion to take ?since=note_0042 as param, device can supply cursor
- [x] Non-goal (v1): no sync cancel (sync is seconds; let it finish). Button presses dropped for its duration
- [ ] Deferred: time-set from Companion `Date` header -> RTC (PCF85063), with the rest of timestamp naming; TZ offset likely in `wifi_secrets.h`
- [ ] Deferred (Tier 2): TLS -- encrypt body + pinned-cert Companion verify. Additive over the token
- [ ] Deferred: throttled live counter (`7/20`) instead of phase-level
- [ ] Deferred: between-file cancel (Menu sets a flag; current file finishes its commit, then bail clean to Menu, never mid-`POST`)

## Deferred

- [ ] WiFi provisioning: USB-serial pairing via companion, or SoftAP (replaces hardcoded creds)
- [ ] Automatic/background sync (pending power budget)
- [ ] Bonus: device-hosted LAN download page (no transcription)
- [ ] Manual time-set UI -- likely unnecessary once sync time-set (Companion `Date` header) works
- [ ] There is a brief pop at the start of all recordings - can we remove it?
