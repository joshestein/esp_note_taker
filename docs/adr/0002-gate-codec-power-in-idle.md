# Power Down the Codec in Idle (in Software), Record the Startup Transient

The ES8311 codec is powered down whenever no Capture is in progress and brought back up only at the start of a recording. This saves the codec's mic-bias / analog current across the long idle periods of a wearable, where battery life dominates.

## Mechanism: software power-down, not a power pin

There is **no board GPIO that gates the codec's supply.** The only firmware-controllable audio power pin, `Audio_PWR_PIN` (GPIO42, net `PA_EN`, active-low), switches the **speaker power amplifier rail (PAVCC via Q6)** -- an output-side rail that is irrelevant to recording. This was verified on hardware: forcing GPIO42 off (rail cut) and recording still produced normal mic audio. So GPIO42 is a **playback** concern; it is left off in Idle and during recording, and switched on only around playback.

Powering the codec down in Idle is therefore done in software: `esp_codec_dev_close()` on Capture stop (register power-down of the ES8311, dropping mic bias) and `esp_codec_dev_open()` at record start. This splits `audio_bsp` into a one-time `init` (I2S driver + I2C bus + codec object creation) and a per-Capture `start`/`stop` (open/close). Merely putting the ESP32-S3 into light sleep does **not** power the codec down -- the codec is an external chip on its own always-on rail, independent of the CPU's sleep state -- so without the explicit close the mic bias would drain continuously through Idle.

## The startup transient is kept, not hidden

Because the codec is cold-started on every Capture, its analog front-end takes tens of milliseconds to settle after `open` reasserts mic bias. We deliberately do **not** detect readiness or discard these first samples -- the recording is naive and keeps every sample from the moment capture starts. The consequence is that every WAV opens with the power-on transient: an audible pop/thump, and possibly a few clipped samples if the ADC saturates on startup. This is accepted as a cosmetic artifact on a review-later voice memo; the transient lands before the wearer begins speaking, so no speech is ever lost. A future reader should not add sample-discarding or a "wait until ready" gate thinking the startup pop is a bug -- recording everything is the deliberate choice, chosen for simplicity and for a record path that can never accidentally eat real audio.

The codec's ADC high-pass filter (REG1B/1C) is left enabled. It DC-corrects in hardware, so the transient no longer rides a DC step and the pop is reduced -- without dropping any samples.

Note: this sample-level "keep everything" is separate from the 2-second minimum-Capture rule, which discards an entire short Capture (an accidental tap), not samples within a Capture that is kept.

## Considered Options

- **Leave codec open in Idle**: no startup transient and no per-Capture re-open latency, but the ES8311 mic bias drains continuously (a few mA) across the idle periods that dominate a wearable's life. Ruled out on battery.
- **Gate the codec supply with a hardware pin**: cleanest power cut, but no such pin exists on this board -- `Audio_PWR_PIN` gates only the speaker amp. Not available.
- **Power down, then detect readiness (signal-based) or discard a fixed warm-up window**: removes the startup pop, but the codec exposes no ready/settled status (its `is_open`/`enabled` flags are software-only), so readiness must be inferred from the signal or guessed as a duration -- extra state and a threshold constant on the hot record path, for a cosmetic gain. Ruled out in favor of naive recording.
