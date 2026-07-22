# ESP32-S3 Voice Note Device -- Glossary

## Capture
A single audio recording session. Begins on a Record Button press and ends on the next. Stored as one WAV file on the SD card. Discarded if shorter than 2 seconds.

## Record Button
The BOOT button (GPIO0). Wakes the device from Idle and immediately begins a Capture; a second press ends it. In the Menu it performs the Card action.

## Menu Button
The PWR button (GPIO18); the code names it `MENU_BUTTON`. Behaviors, all context-dependent:
- Short press from Idle: enter the Menu.
- Short press in Menu: One-card stepping.
- Long press (~1s) in Menu: exit back toward Idle. This is the only long-press meaning; no hold parks the device (see ADR 0007).
- During a Capture: ignored (dropped, not queued).

## Menu
A mode, mutually exclusive with recording: no Capture can start while in the Menu, and the Record Button is repurposed to the Card action. Entered from Idle via a short Menu Button press. Cards: Sync and Sleep, both on screen at once. Flat: every card is a leaf with one action; no sub-lists, no confirm steps. (Storage and Recordings are designed but unbuilt; Recordings is the first that would need a second level.)

## Card
One tile in the Menu: a label plus (except Storage) a single Card action. Built: Sync, Sleep. Designed, unbuilt: Storage, Recordings.

## Wearer
The person using the device. Assumed to operate it by feel -- two buttons, an LED, glanceable e-paper -- often without looking closely. Motivates the always-on indicators and the no-confirm Menu.

## One-card stepping
A Menu Button press advances the Selection by exactly one card. Names the step granularity, not the visibility: the Menu shows all cards at once, while the Recordings list (unbounded) would show one Capture at a time. Cards form a ring -- stepping past the last wraps to the first, and never leaves the Menu.

## Selection
The one card the Record Button will act on. Rendered filled; unselected cards are outlines. Never sticky: entering the Menu always resets it to Sync.

## Menu timeout
~30s with no button press auto-exits the Menu to Idle. Suspended during Syncing.

## Card action
What the Record Button does on the Selection. Fires immediately -- no confirmation step. Sync: run the Sync handshake. Storage: none (read-only). Sleep: enter Deep Sleep.

## Recordings (menu card)
**Designed, not built** (depends on the unbuilt playback path). The Menu card listing existing Captures newest-first, one at a time. The Record Button plays the shown Capture through the speaker.

## Sync (menu card)
The Menu card that connects to Wi-Fi, pushes Captures to the Companion on the LAN, and pulls Transcripts back. The device's RTC is set here from the Companion's HTTP `Date` header. See ADR 0003, ADR 0006.

## Sync trigger
Sync is manual: the radio stays off until a Record Button press on the Sync card triggers it. See ADR 0003.

## Sync handshake
The two-phase sync, device always the HTTP client. Upload: POST each unsynced Capture; on a 200, move it to the Synced folder. Download: fetch the ready Transcripts it lacks. Both directions idempotent by filename. A **per-file failure** (timeout, 500, dropped connection) is **skipped, not fatal** -- the item stays unsynced, is retried next Sync, and the session continues. Only a Session-level failure ends the session. Full contract in `docs/sync-protocol.md`.

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
**Designed, not built.** The Menu card showing free space and number of Captures. Read-only: no Card action. Erasing Captures on the device is a **non-goal** -- freeing space is done on the Companion.

## Sleep (menu card)
The Menu card whose Card action enters Deep Sleep. The only way to park the device. See ADR 0007.

## Battery ring
A segmented ring on the Idle screen: five battery bands (Critical/Low/Medium/High/Full), one filled segment each, all-outline before the first read. Voltage-inferred, not a fuel gauge: sags under load, reads high while charging. **Not live** -- reflects the last Idle repaint (boot, capture end, menu exit), since Idle is Light Sleep. Idle screen only. Display-only in v1: Critical does not stop a Capture (see Auto-stop).

## Idle
The device state when no Capture is in progress. The CPU is in Light Sleep; either button wakes it. The e-paper display retains its last image; the audio codec is powered down (see ADR 0002); the SD card stays mounted for fast record start.

## Recording State
The device state during an active Capture: audio is read from the codec and written in chunks to an open WAV file. Recording is naive: every sample is kept from the moment capture starts, none discarded. (Distinct from the 2-second minimum-Capture rule under Capture, which discards a whole short Capture, not samples within one.)

## Finalizing
The device state after a Capture ends, while the WAV header is patched and the file closed. A broken Capture (failed write, or a task that never started) is `remove()`d here, not saved. Reached only when the record task signals it has stopped (see ADR 0004). Button presses during Finalizing are dropped, not queued.

## Syncing
The device state during an active Sync: the radio is on and a dedicated sync task runs the Sync handshake (connect, mDNS resolve, uploads, downloads, disconnect) while the task signals its progress and completion back to the main loop. A top-level peer of Recording State. Entered from the Sync card, which it **leaves** -- the Menu closes when a Sync starts, and the device lands in Idle when it ends, showing the Sync result. Mutually exclusive with recording; button presses are dropped for its duration (no cancel in v1). See ADR 0003, ADR 0006.

## Sync result
The outcome of a Sync, painted over the Idle screen when the Sync ends. Does **not** expire: the device is genuinely Idle underneath, and the next Record or Menu press acts from Idle and repaints -- so the resting image is the last Sync's outcome, not a plain "ready". Shows **failures, not just successes** ("3 up, 1 down, 2 failed"). An unauthorized Sync (401) reports as such rather than as a generic failure -- it is permanent until reflash, not transient.

## Session-level failure
A Sync failure that ends the whole session: Wi-Fi won't connect (no saved network is in range, or the one present won't associate), mDNS won't resolve, or the Companion returns 401. Retrying files against a Companion you cannot reach or are not authorized for is pointless, so the session aborts (radio off, Error Indication, Sync result). Distinct from a **per-file failure**, which is skipped -- see Sync handshake.

## Light Sleep
The sleep mode used during Idle: CPU paused, RAM retained, GPIO interrupts wake in ~1ms. See ADR 0001. Distinct from Deep Sleep.

## Deep Sleep
The parked, lowest-drain state. Entered from the Sleep card (menu card). CPU and RAM power down (RAM is lost), so waking is a full cold boot. Both buttons wake it: the Menu Button to Idle, the Record Button straight into a Capture (accepting a clipped opening). See ADR 0007.

## WAV File
The storage format for each Capture: PCM, 16kHz, mono, 16-bit. Named at Capture start by one of two regimes, selected by whether the RTC holds valid time (see RTC): a trusted clock gives `note_YYYYMMDD_HHMMSS.wav`; an untrusted clock (before the clock has ever been set) falls back to a scanned monotonic sequence `note_NNNN.wav`. The counter regime is reachable only before the first Sync, when nothing has yet reached the Companion. The header's length fields are patched on close, so a power loss mid-Capture leaves that one file unplayable.

## Auto-stop
A Capture has no fixed time limit; it runs until the next Record Button press. It ends early only on a resource threshold (low battery or near-full SD card), stopping cleanly so the memo survives. Thresholds are deferred.

## Recording Indicator
An LED held steady for the full duration of a Capture, off otherwise. Lets the wearer confirm a session is active without close inspection.

## Recording Screen
The e-paper image shown for the duration of a Capture: a filled circle glyph plus the word "recording". Distinct from the Recording Indicator (the LED) -- same meaning, different surface.

## Display
The e-paper panel. A device that cannot paint cannot be driven -- the Menu, the Selection, the Sync result, and Idle live only there -- so its init is **fatal**, like the SD card's and the codec's, with no degraded blind mode. Not a gate on recording: it initialises *after* the Capture starts on a Record-Button wake, so its slow bring-up never clips audio off the front of a memo. See ADR 0005.

## Error Indication
A distinct LED pattern (e.g. blinking, unlike the steady Recording Indicator) signalling a failure the wearer must act on: no SD card at record start, card full, or card removed mid-Capture.

## SD Card
The storage medium for WAV files. Required for device operation.

## RTC
The PCF85063 real-time clock (I2C 0x51), battery-backed so it keeps time across deep sleep, power loss, and an SD wipe. Two roles: the timestamp source for WAV naming, and the **trust signal** that selects the naming regime -- its oscillator-stop bit reports whether time is valid, so the "have we ever set the clock?" answer lives in the battery-backed chip rather than on the SD card (see WAV File). Time is set at Sync from the Companion's `Date` header; until first set the clock reads untrusted and naming falls back to the sequence counter.

---

## Unfiled rationale (no ADR home yet)

Reasoning trimmed from the glossary that isn't captured in any ADR. Each is a small ADR waiting to be written, or a note to fold into an existing one. Not glossary content -- parked here so it isn't lost.

- **All Menu cards visible at once, with a filled Selection:** a whole-screen partial refresh is ~0.3s whether one card or three change, so showing all three costs nothing over one-at-a-time, and the wearer can see where the Selection is instead of stepping blind. The old "only repaint one card region" reasoning still holds for the unbounded Recordings list, which is why visibility is per-level. (Candidate: fold into ADR 0005.)
- **No confirm step on any Card action:** possible only because no destructive action lives on the device (see Storage). Keeps the Menu flat.
- **Selection resets to Sync on Menu entry:** a sticky Selection could open the Menu pre-armed on Sleep, the one card whose mis-press costs a cold boot.
- **Full refresh on Menu exit, partials within it:** the ~2s flash lands when the wearer is walking away, and guarantees Idle -- the screen shown 99% of the time -- is always cleanly written.
- **WAV mono:** mono because the ES8311 codec has a single mic; the 2-channel manufacturer example was loopback playback, not memo storage.
- **Recording Indicator over haptic/audio cues:** LED chosen for low power and simplicity.
- **Per-file Sync failures skipped, not fatal:** aborting would make one unsendable Capture a poison pill -- enumerated oldest-first, it would abort every future Sync and silently block backup of everything behind it. (Candidate: fold into ADR 0003.)
- **Sync result persists as the resting screen:** e-paper holds it free, and a silent Sync failure would otherwise leave the wearer believing their Captures are backed up; a partial Sync that reads as complete is the exact failure this screen exists to prevent. (Candidate: fold into ADR 0003.)
