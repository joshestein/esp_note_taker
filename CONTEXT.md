# ESP32-S3 Voice Note Device -- Glossary

## Capture
A single audio recording session. Begins when the user presses the Record Button and ends when they press it again. Stored as one WAV file on the SD card. Discarded if shorter than 2 seconds.

## Record Button
The BOOT button (GPIO0). Wakes the device from Idle and immediately begins a Capture. A second press ends the Capture.

## Menu Button
The PWR button (GPIO18). A short press from Idle enters the Menu. Inside the Menu it means "next" (step to the next card). A long press exits back toward Idle. A long press from Idle powers the device off/to sleep.

## Menu
A mode, mutually exclusive with recording -- you cannot start a Capture while in the Menu; the Record Button is repurposed to "act on this card" (e.g. play). Entered from Idle via a short Menu Button press. Navigated one card at a time (see **One-card stepping**). Top-level cards: **Recordings**, **Sync**, **Storage**.

## One-card stepping
The navigation model for the Menu: exactly one item is shown on screen at a time, and the Menu Button steps to the next one. Chosen over a scrolling multi-item list because the e-paper display refreshes slowly and only a single card region needs repainting per step.

## Recordings (menu card)
The Menu card that lists existing Captures newest-first (sequence filenames sort naturally), stepped one at a time. The Record Button plays the shown Capture through the speaker; playback uses the same ES8311 codec (re-opened as for recording) plus the speaker amplifier, whose power (`Audio_PWR_PIN`, GPIO42) is switched on only for playback.

## Sync (menu card)
The Menu card that connects to Wi-Fi and pushes Captures to a paired computer on the same LAN, which runs the AI transcription and keeps the files (see ADR 0003). Transcription runs off-device (too large for the ESP32-S3). Correct RTC time is obtained here via NTP rather than a manual time-setting UI. Design is in progress.

## Sync trigger
Sync is **manual**: entering the Sync card and pressing the Record Button connects Wi-Fi, uploads unsynced Captures, then disconnects. The radio stays off until deliberately triggered -- chosen to protect battery on the wearable; automatic/background sync is deferred until the power budget is known.

## Sync handshake
A sync has two phases, both with the device as HTTP client. **Upload:** the device POSTs each unsynced WAV to a listener on the paired computer; the computer writes the file, flushes it to disk, and responds 200 with the stored filename/size. Only on that 200 does the device move the Capture to the **Synced folder**. **Download:** the device GETs the list of ready transcripts and fetches any it does not yet have (see **Transcript**). Any timeout, non-200, or dropped connection leaves the affected item for retry; both directions are idempotent (dedup by filename).

## Transcript
The plain-text transcription of a Capture, produced by the Companion's local Whisper and sent **back** to the device on a later sync (transcription is not instant, so a Capture's Transcript arrives on a subsequent sync, not the one that uploaded it -- the system is eventually-consistent). Stored at `/sdcard/transcripts/note_YYYYMMDD_HHMMSS.txt`, paired to its WAV by filename stem. The device only **holds** Transcripts (and may show a one-line preview on the Recordings card); it does not provide a full on-device reader -- real reading happens on the computer. Holding them keeps a future paging reader as a purely additive change.

## Companion
The program running on the paired computer that receives uploaded Captures, transcribes them, and stores them. Lives in a **separate repository** (different language/runtime; Python + `faster-whisper`). Transcription is **local Whisper**, not a cloud API -- keeping audio on the LAN is the whole reason architecture B was chosen (see ADR 0003). Per memo it emits a plain-text transcript and a JSON transcript (segments + timestamps) alongside the WAV. From this firmware's side the Companion is just an HTTP endpoint honoring the **Sync handshake**; its internals are out of scope for this repo.

## Companion discovery
How the device locates its Companion's current IP on the LAN. The Companion advertises a **per-owner mDNS hostname** (e.g. `josh-memo.local`); each device hardcodes the single hostname it belongs to (in the gitignored `wifi_secrets.h`, alongside Wi-Fi creds) and resolves it by mDNS at sync start. Chosen over a hardcoded IP (breaks when DHCP moves the address) and over DNS-SD service browsing with a pairing token (correct for a larger fleet, but overkill for a few hand-configured devices). With multiple devices on one LAN, uniqueness is guaranteed by giving each owner's Companion a distinct hostname. Because Wi-Fi is shared, all a household's devices carry identical Wi-Fi creds and differ only in this target hostname. Discovery (hostname vs service-browse) is orthogonal to **Sync auth**: moving to service browsing later is an additive change.

**1:1 invariant:** a Companion serves exactly one device. Capture filenames (`note_NNNN`) come from a per-device counter and are only unique *within* a device, so pointing two devices at one Companion would collide (one device's `note_0003` overwrites the other's on upload). Per-owner pairing preserves this by construction. If 1:1 is ever broken (shared laptop, reflashed/reset counter re-syncing to a populated Companion), the fix is a per-device id prefix in the filename (`leo_note_0007`), hardcoded alongside the other `wifi_secrets.h` values -- deferred, since v1 is three devices each paired to their own laptop.

## Sync auth
A single shared bearer token, hardcoded on both sides (device in `wifi_secrets.h`, Companion in its own config), sent as an `Authorization: Bearer <token>` header on every Sync request. The Companion rejects any request lacking it (401), which stops other machines on an untrusted LAN from POSTing junk Captures or scraping Transcripts, and stops the device from accepting a Companion that cannot echo the secret. Transport stays **plaintext HTTP** for now, so the token and the WAV body are still sniffable by a passive attacker already on the network -- an accepted risk for the manual, seconds-long sync window. Upgrading to TLS (encrypt the wire, verify Companion identity via a pinned cert) is deferred and is an additive change (`http://` -> `https://` + cert).

## Synced folder
`/sdcard/synced/`. A Capture that has been confirmed-received by the paired computer is `rename()`d from the record folder into here. Captures are **kept, not deleted**, after sync -- the card is a durable archive. An unsynced Capture is simply one still in the record folder, so an interrupted upload retries next time with no separate index to corrupt. Freeing space (erasing synced Captures) is a manual action under the Storage card.

## Storage (menu card)
The Menu card showing card status: free space and number of Captures, and possibly an erase-all action.

## Battery glyph
Battery level is shown as an always-visible glyph on the Idle screen (read via `VBAT_PWR_PIN`, GPIO17), not as a Menu card -- battery status is wanted at a glance, not buried in navigation.

## Idle
The device state when no Capture is in progress. The CPU is in light sleep; GPIO interrupts from either button trigger immediate wake. The e-paper display retains the last drawn image without power. The audio codec is powered down in Idle to stop its mic-bias drain -- no board pin gates the codec's supply, so this is a **software** power-down (the codec is closed on entering Idle and re-opened at record start), not a rail cut. `Audio_PWR_PIN` (GPIO42) gates only the speaker amplifier and stays off except during playback. The SD card stays mounted for fast record start.

## Recording State
The device state during an active Capture. Audio is read from the codec and written in chunks to an open WAV file on the SD card. Recording is naive: every sample is kept from the moment capture starts, none detected or discarded. (This is separate from the 2-second minimum-Capture rule under **Capture**, which discards a whole short Capture, not samples within one.)

## Finalizing
The device state after a Capture ends, while the WAV header is patched and the file is closed on the SD card. A broken Capture (failed write, or a task that never started) is `remove()`d here, not saved -- distinct from the <2s short-Capture discard under **Capture**, which drops a complete-but-too-short one. Reached only when the record task signals it has stopped (see ADR 0004). Button presses during Finalizing are dropped, not queued -- a press here does not start a new Capture. Prevents accidental stacked recordings on a pocket-worn device and avoids racing the still-open file handle.

## Light Sleep
The ESP32-S3 sleep mode used during Idle. CPU is paused, RAM is retained, GPIO interrupts wake the device in ~1ms. Chosen over deep sleep to allow instant Capture start on button press.

## WAV File
The storage format for each Capture. PCM, 16kHz sample rate, 1 channel (mono), 16-bit depth. Mono because the ES8311 codec has a single mic; the 2-channel manufacturer example was loopback playback, not memo storage. Named by a monotonic sequence number in v1: `note_NNNN.wav`, assigned at Capture start and increasing per Capture so newest sorts last. Timestamp naming (`note_YYYYMMDD_HHMMSS.wav`) is deferred until the clock is trustworthy (set via Sync/NTP); the RTC exists but is unset on a fresh device. The header's length fields are written only when the Capture ends (patched on close), so a sudden power loss mid-Capture leaves that one file unplayable -- an accepted risk, not mitigated by periodic flushing.

## Auto-stop
A Capture has no fixed time limit; it runs until the wearer presses Record again. It ends early only on a resource threshold -- low battery or near-full SD card -- stopping cleanly (finalize + close) so the memo survives rather than being lost to a dead battery or full card. Thresholds are deferred.

## Recording Indicator
An LED held steady for the full duration of a Capture, off otherwise. Lets the wearer confirm a session is active without close inspection. Chosen over haptic/audio cues for low power and simplicity.

## Recording Screen
The e-paper image shown for the full duration of a Capture: a filled circle glyph plus the word "recording". Distinct from the **Recording Indicator** (the LED) -- same meaning, different surface, painted once on entering the **Recording State** and replaced on return to **Idle**.

## Error Indication
A distinct LED pattern (e.g. blinking, unlike the steady Recording Indicator) signalling a failure the wearer must act on: no SD card at record start, card full, or card removed mid-Capture. The record task aborts to Finalizing (or straight to Idle if the card is gone) on any `fwrite` failure and raises this indication.

## SD Card
The storage medium for WAV files. Required for device operation. Exposed via `sdcard_bsp` from the manufacturer example.

## RTC
The PCF85063 real-time clock (I2C 0x51). Intended source of timestamps for WAV file naming, but unused in v1 (Captures use a sequence number -- see **WAV File**). Its supply is diode-OR'd from the 3V3 rail and the main battery, so once set it holds time through light sleep and soft power-off, losing it only if the battery is drained or removed. Time-setting is deferred to Sync/NTP; until then the clock is untrusted and naming stays sequence-based.
