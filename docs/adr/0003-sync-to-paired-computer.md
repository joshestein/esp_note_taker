# Sync to a Paired Computer on the LAN, Not the Cloud

Captures are synced by the device pushing them over the local network to a companion program on the user's own computer, which runs the AI transcription (local Whisper) and keeps the files. We deliberately do **not** upload to a cloud service. Voice memos are intimate; keeping them on the user's own hardware with local transcription means the audio never leaves the LAN, and it avoids having to build, host, pay for, and secure a backend. The accepted cost is that sync only works at home (on the same network as the computer) and requires a small companion program running there -- both fine for a personal memo recorder, where "sync when I get home" is the natural model.

The device is the HTTP client: it POSTs each unsynced WAV to the companion, which stores the file, flushes it to disk, and returns 200. Only on that 200 does the device move the Capture into `/sdcard/synced/`. Captures are kept, not deleted, after sync -- the SD card is a durable archive, and a memo recorder that erased its originals after a network transfer would be one bug away from data loss.

Sync is bidirectional: after uploading, the device also pulls finished transcripts back and stores them on the card (`/sdcard/transcripts/`). The device stays the HTTP client for this too (GET a list, fetch what it lacks), so the ESP32 never has to run a server. Because transcription is not instant, a Capture's transcript arrives on a *later* sync than the one that uploaded it -- the system is deliberately eventually-consistent. The device only holds transcripts (at most a one-line preview on the Recordings card); it does not provide a full on-device reader, since 200x200 e-paper makes real reading better done on the computer. Holding them leaves a future paging reader as a purely additive change.

Transcription runs off-device because a Whisper-class model does not fit on the ESP32-S3. Local Whisper (`faster-whisper`) is chosen over a cloud transcription API specifically to preserve the privacy that motivated the whole architecture -- calling a cloud API would reopen the exact hole this design closes.

## Considered Options

- **Cloud service (device uploads to a hosted backend with a web UI)**: works away from home and needs nothing running on the user's computer, but requires building/hosting/paying for a backend, sends intimate audio to a third party, and needs device-side cloud auth. Ruled out; remote access is not a requirement.
- **Device hosts a LAN web server for download**: no companion program, but the device cannot transcribe, so it would be download-only, and it would have to stay awake on Wi-Fi to serve, costing battery. May be added later as a bonus download path, but it does not meet the transcription goal on its own.
- **Cloud transcription API with local storage**: keeps files local but still ships the audio to a third party for transcription, contradicting the privacy rationale. Ruled out in favor of local Whisper.

## Notes

- Wi-Fi credentials (and, for now, the computer's address) are hard-coded as a prototype shortcut and must stay in a gitignored file, never committed. A real provisioning path (USB-serial pairing via the companion, or SoftAP) is deferred.
- Sync is manual and radio-off-until-triggered to protect battery; automatic/background sync is deferred until the power budget is known.
- Correct RTC time is obtained via NTP during a sync, so no manual time-setting UI is needed.
