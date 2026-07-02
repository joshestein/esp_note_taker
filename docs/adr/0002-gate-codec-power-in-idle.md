# Gate Audio Codec Power in Idle, Record the Startup Transient

The ES8311 codec is powered off (via `Audio_PWR_PIN`) whenever no Capture is in progress, and powered on only at the start of a recording. This saves the codec's mic-bias current across the long idle periods of a wearable, where battery life dominates.

Because the codec is cold-started on every Capture, its analog front-end (and the external mic-bias/LDO rail gated by `Audio_PWR_PIN`) takes tens of milliseconds to settle. We deliberately do **not** detect readiness or discard these first samples -- the recording is naive and keeps every sample from the moment capture starts. The consequence is that every WAV opens with the power-on transient: an audible pop/thump, and possibly a few clipped samples if the ADC saturates on startup. This is accepted as a cosmetic artifact on a review-later voice memo; the transient lands before the wearer begins speaking, so no speech is ever lost. A future reader should not add sample-discarding or a "wait until ready" gate thinking the startup pop is a bug -- recording everything is the deliberate choice, chosen for simplicity and for a record path that can never accidentally eat real audio.

The codec's ADC high-pass filter (REG1B/1C) is left enabled. It DC-corrects in hardware, so the transient no longer rides a DC step and the pop is reduced -- without dropping any samples.

Note: this sample-level "keep everything" is separate from the 2-second minimum-Capture rule, which discards an entire short Capture (an accidental tap), not samples within a Capture that is kept.

## Considered Options

- **Leave codec powered in Idle**: no startup transient, but constant mic-bias drain shortens battery life on a device that sits idle most of the time. Ruled out for a wearable.
- **Gate power, then detect readiness (signal-based) or discard a fixed warm-up window**: removes the startup pop, but the codec exposes no ready/settled status (its `is_open`/`enabled` flags are software-only), so readiness must be inferred from the signal or guessed as a duration -- extra state and a threshold constant on the hot record path, for a cosmetic gain. Ruled out in favor of naive recording.
