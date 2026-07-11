# Two-Tier Sleep: Deep-Sleep Park Under Light-Sleep Idle

ADR 0001 chose **light sleep** for Idle so the Record Button starts a Capture on the same press that wakes the device (~1ms resume, RAM retained). That decision stands for the everyday Idle&harr;record path. But light-sleep Idle still draws ~1-5mA, which drains the battery over days of non-use -- there was no way to deliberately *park* the device for storage.

This ADR adds a **second sleep tier** on top of ADR 0001 without changing it: a **Deep Sleep** "parked" state, entered only by an explicit ~2s long press of the Menu Button from Idle. CPU and RAM power down (~10µA). Because RAM is lost, waking is a full cold boot -- remount SD, rescan the note counter, re-init the codec (~0.4-1s) -- versus the ~1ms light-sleep resume.

Both buttons wake from Park, with different targets:
- **Menu Button** wakes to Idle.
- **Record Button** wakes straight into a Capture, branching on `esp_sleep_get_wakeup_cause()` at the top of `app_main`.

The Record-wake path **accepts a clipped opening**: the first ~0.5-1s of audio is lost while the codec spins up during the cold boot. This is the one place we knowingly break ADR 0001's "instant capture" guarantee -- and we scope the break precisely: **the instant-capture guarantee is a light-sleep/Idle property only, not a Park property.** From Idle, no clip. From Park, the user accepts a clipped first word as the cost of the low-drain state they explicitly chose.

Two thresholds guard the gestures: **2s** to park (from Idle), **1s** to exit the Menu. The longer park hold resists accidental triggering against the body on a pocket-worn device; a false park is recoverable (any press wakes it) but silent and annoying, so it is worth making deliberate.

## Considered Options

- **No off state (light-sleep Idle only)**: least code, no clip ever. Ruled out because a wearable left unused for days needs a true low-drain state; ~1-5mA is not "stored away."
- **Deep-sleep park, Record-wake buffers the intent through cold boot to honor instant-capture**: preserves the no-clip guarantee everywhere. Ruled out because it means promising a recording before SD mount / counter rescan have succeeded; if init fails, the promise is broken with audio already "started." Cleaner to scope the guarantee to Idle.
- **Deep-sleep park, both buttons wake only to Idle (no wake-to-record)**: no clip, but a user who grabs the parked device to capture a thought must press twice and wait. Ruled out as a worse capture UX for the device's core purpose; the clip is the lesser cost.
- **Single long-press threshold for both park and Menu-exit**: less code. Ruled out because a 2s Menu-exit feels sluggish and a 1s park is too easy to trigger in a pocket; the two gestures have very different accident costs.
