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
The Menu card that lists existing Captures newest-first (timestamp filenames sort naturally), stepped one at a time. The Record Button plays the shown Capture through the speaker; playback uses the same ES8311 codec, so it must be powered on for playback as it is for recording.

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

## Synced folder
`/sdcard/synced/`. A Capture that has been confirmed-received by the paired computer is `rename()`d from the record folder into here. Captures are **kept, not deleted**, after sync -- the card is a durable archive. An unsynced Capture is simply one still in the record folder, so an interrupted upload retries next time with no separate index to corrupt. Freeing space (erasing synced Captures) is a manual action under the Storage card.

## Storage (menu card)
The Menu card showing card status: free space and number of Captures, and possibly an erase-all action.

## Battery glyph
Battery level is shown as an always-visible glyph on the Idle screen (read via `VBAT_PWR_PIN`, GPIO17), not as a Menu card -- battery status is wanted at a glance, not buried in navigation.

## Idle
The device state when no Capture is in progress. The CPU is in light sleep; GPIO interrupts from either button trigger immediate wake. The e-paper display retains the last drawn image without power. The audio codec is powered off (via `Audio_PWR_PIN`) to save battery; the SD card stays mounted for fast record start.

## Recording State
The device state during an active Capture. Audio is read from the codec and written in chunks to an open WAV file on the SD card. Recording is naive: every sample is kept from the moment capture starts, including the codec's power-on transient (an audible pop at the start of each file), which is accepted rather than detected or discarded. The codec's ADC high-pass filter is left enabled to DC-correct in hardware and soften that pop without dropping samples. (This is separate from the 2-second minimum-Capture rule under **Capture**, which discards a whole short Capture, not samples within one.)

## Finalizing
The device state after a Capture ends, while the record task patches the WAV header and closes the file on the SD card. Button presses during Finalizing are dropped, not queued -- a press here does not start a new Capture. Prevents accidental stacked recordings on a pocket-worn device and avoids racing the still-open file handle.

## Light Sleep
The ESP32-S3 sleep mode used during Idle. CPU is paused, RAM is retained, GPIO interrupts wake the device in ~1ms. Chosen over deep sleep to allow instant Capture start on button press.

## WAV File
The storage format for each Capture. PCM, 16kHz sample rate, 1 channel (mono), 16-bit depth. Mono because the ES8311 codec has a single mic; the 2-channel manufacturer example was loopback playback, not memo storage. Named by timestamp: `note_YYYYMMDD_HHMMSS.wav`. The header's length fields are written only when the Capture ends (patched on close), so a sudden power loss mid-Capture leaves that one file unplayable -- an accepted risk, not mitigated by periodic flushing.

## Auto-stop
A Capture has no fixed time limit; it runs until the wearer presses Record again. It ends early only on a resource threshold -- low battery or near-full SD card -- stopping cleanly (finalize + close) so the memo survives rather than being lost to a dead battery or full card. Thresholds are deferred.

## Recording Indicator
An LED held steady for the full duration of a Capture, off otherwise. Lets the wearer confirm a session is active without close inspection. Chosen over haptic/audio cues for low power and simplicity.

## Error Indication
A distinct LED pattern (e.g. blinking, unlike the steady Recording Indicator) signalling a failure the wearer must act on: no SD card at record start, card full, or card removed mid-Capture. The record task aborts to Finalizing (or straight to Idle if the card is gone) on any `fwrite` failure and raises this indication.

## SD Card
The storage medium for WAV files. Required for device operation. Exposed via `sdcard_bsp` from the manufacturer example.

## RTC
The PCF85063 real-time clock (I2C 0x51). Provides timestamps for WAV file naming. Time-setting is deferred to Menu implementation.
