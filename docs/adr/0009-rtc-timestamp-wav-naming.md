# RTC-timestamp WAV naming, with a pre-sync counter fallback

## Context

Capture identity, dedup key, and sequence were all one thing: the `note_NNNN.wav`
stem, with the counter derived by scanning the SD card at boot (`sdcard_scan_max`).
That makes identity a function of SD state, so wiping the card (user deletes files
on a computer, reformats, or swaps in a fresh card) resets the counter. The next
Capture reuses a live name, and because upload is idempotent-by-filename
(overwrite-and-200), it silently clobbers a different memo already backed up on the
Companion; the download phase symmetrically re-pulls old transcripts and mispairs
them with new audio. This is the exact data loss Sync exists to prevent.

## Decision

Name Captures from the battery-backed PCF85063 RTC: `note_YYYYMMDD_HHMMSS.wav` once
the clock holds valid time, falling back to the scanned counter `note_NNNN.wav` only
while the clock has never been set. **The "is the clock trusted?" signal is read
from the RTC's oscillator-stop bit, not from any SD or NVS flag** — so it survives an
SD wipe. Time is set at Sync from the Companion's `Date` header.

Timestamps are monotonic for free (the device records one Capture at a time, so no
same-second collision) and identity is no longer derived from SD state, so an SD wipe
can no longer reissue a name. The counter regime is reachable only before the first
Sync, when nothing has yet reached the Companion — so its reset-on-wipe has nothing
to clobber. Deep sleep does not drift the clock: the PCF85063 is a separate always-
powered chip, not the ESP internal RTC. Residual crystal drift (~±20 ppm) is
re-corrected from the `Date` header every Sync.

## Considered and rejected

- **Persist the counter in NVS.** Survives SD wipe, but NVS is not battery-backed
  the way the RTC is, keeps the resettable-sequence model, and reintroduces the
  on-flash state the design deliberately avoids. Timestamps solve the same problem
  using a clock we already need.
- **Force / prompt a Sync before the first recording.** Breaks the core promise that
  a Record-Button press records immediately; a device with no reachable Companion
  (fresh unbox, away from home LAN) could not capture at all.
- **No-clobber on the Companion** (reject an upload whose name exists with different
  content, 409 instead of overwrite). **Deferred, not adopted.** With the above,
  in-scope collisions (SD wipe pre/post first Sync) are structurally impossible, so
  no-clobber only guards the out-of-scope tail (device swap onto an old Companion,
  a wrong `Date` header) at the cost of breaking the clean idempotent-by-filename
  contract (retries must still 200, forcing content comparison). Revisit if device
  rehoming becomes a supported flow.

## Consequences

- Numeric and timestamp stems coexist on device and Companion; the transcript diff
  is string-based and format-agnostic, so no migration of existing `note_NNNN` files
  is needed.
- A fully dead device (RTC backup power lost) falls back to the counter regime on
  cold boot — treated as out of scope, same class as device replacement.
