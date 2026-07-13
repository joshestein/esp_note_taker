# ESP32-S3 Voice Note Device -- Glossary

## Capture
A single audio recording session. Begins on a Record Button press and ends on the next. Stored as one WAV file on the SD card. Discarded if shorter than 2 seconds.

## Record Button
The BOOT button (GPIO0). Wakes the device from Idle and immediately begins a Capture; a second press ends it. In the Menu it is repurposed to "act on this card" (Card action).

## Menu Button
The PWR button (GPIO18); the code names it `MENU_BUTTON`. Behaviors, all context-dependent:
- Short press from Idle: enter the Menu.
- Short press in Menu: step to the next card (One-card stepping).
- Long press (~1s) in Menu: exit back toward Idle.
- Long press (~2s) from Idle: enter Deep Sleep (see ADR 0007).
- During a Capture: ignored (dropped, not queued).

## Menu
A mode, mutually exclusive with recording: no Capture can start while in the Menu, and the Record Button is repurposed to "act on this card." Entered from Idle via a short Menu Button press. Navigated by One-card stepping. Top-level cards: Recordings, Sync, Storage.

## One-card stepping
The Menu's navigation model: exactly one item is on screen at a time, and the Menu Button steps to the next.

## Recordings (menu card)
The Menu card listing existing Captures newest-first, stepped one at a time. The Record Button plays the shown Capture through the speaker.

## Sync (menu card)
The Menu card that connects to Wi-Fi, pushes Captures to the Companion on the LAN, and pulls Transcripts back. The device's RTC is set here from the Companion's HTTP `Date` header. See ADR 0003, ADR 0006.

## Sync trigger
Sync is manual: on the Sync card, a Record Button press connects Wi-Fi, uploads unsynced Captures, then disconnects. The radio stays off until triggered. See ADR 0003.

## Sync handshake
The two-phase sync, with the device always the HTTP client. Upload: the device POSTs each unsynced Capture; on a 200 it moves the Capture to the Synced folder. Download: the device fetches the ready Transcripts it lacks. Any failure leaves that item for retry; both directions are idempotent by filename. Full contract in `docs/sync-protocol.md`.

## Transcript
The plain-text transcription of a Capture, produced by the Companion and sent back on a later sync (eventually-consistent). Paired to its WAV by filename stem. The device only holds Transcripts (and may show a one-line preview on the Recordings card); it provides no full on-device reader. See ADR 0003.

## Companion
The program on the paired computer that receives Captures, transcribes them (local Whisper), stores them, and serves Transcripts back. From the firmware's side, just an HTTP endpoint honoring the Sync handshake. Lives in the `companion/` subtree (Python + Flask). See ADR 0003, ADR 0008.

## Companion discovery
How the device locates its Companion on the LAN: it resolves a per-owner mDNS hostname (e.g. `josh-memo.local`, held in the gitignored `wifi_secrets.h`) at sync start. **1:1 invariant:** one Companion serves exactly one device. See ADR 0006.

## Sync auth
A shared bearer token, hardcoded on both sides, sent as `Authorization: Bearer <token>` on every Sync request; the Companion 401s any request missing or carrying the wrong token. Transport is plaintext HTTP for now. See ADR 0006.

## Synced folder
`/sdcard/synced/`. A Capture confirmed-received by the Companion is `rename()`d here from the record folder. Captures are kept, not deleted, after sync. Freeing space is a manual action under the Storage card. See ADR 0003.

## Storage (menu card)
The Menu card showing card status: free space, number of Captures, and possibly an erase-all action.

## Battery glyph
An always-visible battery-level glyph on the Idle screen (read via `VBAT_PWR_PIN`, GPIO17), not a Menu card.

## Idle
The device state when no Capture is in progress. The CPU is in Light Sleep; either button wakes it. The e-paper display retains its last image; the audio codec is powered down (see ADR 0002); the SD card stays mounted for fast record start.

## Recording State
The device state during an active Capture: audio is read from the codec and written in chunks to an open WAV file. Recording is naive: every sample is kept from the moment capture starts, none discarded. (Distinct from the 2-second minimum-Capture rule under Capture, which discards a whole short Capture, not samples within one.)

## Finalizing
The device state after a Capture ends, while the WAV header is patched and the file closed. A broken Capture (failed write, or a task that never started) is `remove()`d here, not saved. Reached only when the record task signals it has stopped (see ADR 0004). Button presses during Finalizing are dropped, not queued.

## Syncing
The device state during an active Sync: the radio is on and a dedicated sync task runs the Sync handshake (connect, mDNS resolve, uploads, downloads, disconnect) while the task signals its progress and completion back to the main loop. A top-level peer of Recording State, entered from the Sync card and returning to it with a result summary ("Synced N up, M down" or a failure). Mutually exclusive with recording; button presses are dropped for its duration (no cancel in v1). See ADR 0003, ADR 0006.

## Light Sleep
The sleep mode used during Idle: CPU paused, RAM retained, GPIO interrupts wake in ~1ms. See ADR 0001. Distinct from Deep Sleep.

## Deep Sleep
The parked, lowest-drain state. Entered from the Sleep card (menu card). CPU and RAM power down (RAM is lost), so waking is a full cold boot. Both buttons wake it: the Menu Button to Idle, the Record Button straight into a Capture (accepting a clipped opening). See ADR 0007.

## WAV File
The storage format for each Capture: PCM, 16kHz, mono, 16-bit. Named by a monotonic sequence in v1: `note_NNNN.wav`, assigned at Capture start. Timestamp naming (`note_YYYYMMDD_HHMMSS.wav`) is deferred until the clock is trustworthy. The header's length fields are patched on close, so a power loss mid-Capture leaves that one file unplayable.

## Auto-stop
A Capture has no fixed time limit; it runs until the next Record Button press. It ends early only on a resource threshold (low battery or near-full SD card), stopping cleanly so the memo survives. Thresholds are deferred.

## Recording Indicator
An LED held steady for the full duration of a Capture, off otherwise. Lets the wearer confirm a session is active without close inspection.

## Recording Screen
The e-paper image shown for the duration of a Capture: a filled circle glyph plus the word "recording". Distinct from the Recording Indicator (the LED) -- same meaning, different surface.

## Error Indication
A distinct LED pattern (e.g. blinking, unlike the steady Recording Indicator) signalling a failure the wearer must act on: no SD card at record start, card full, or card removed mid-Capture.

## SD Card
The storage medium for WAV files. Required for device operation.

## RTC
The PCF85063 real-time clock (I2C 0x51). Intended timestamp source for WAV naming, unused in v1 (Captures use a sequence number). Time-setting is deferred to Sync (from the Companion's `Date` header); until then the clock is untrusted.

---

## Unfiled rationale (no ADR home yet)

Reasoning trimmed from the glossary that isn't captured in any ADR. Each is a small ADR waiting to be written, or a note to fold into an existing one. Not glossary content -- parked here so it isn't lost.

- **One-card stepping over a scrolling list:** chosen because e-paper refreshes slowly and only a single card region needs repainting per step, versus repainting a whole multi-item list. (Candidate: fold into ADR 0005.)
- **WAV mono:** mono because the ES8311 codec has a single mic; the 2-channel manufacturer example was loopback playback, not memo storage.
- **Recording Indicator over haptic/audio cues:** LED chosen for low power and simplicity.
