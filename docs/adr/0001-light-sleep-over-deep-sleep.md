# Light Sleep Instead of Deep Sleep for Idle Mode

The manufacturer example uses deep sleep (full CPU off, ~1-2s boot on wake). We use light sleep instead because the core UX requirement is that pressing the Record Button immediately starts a Capture -- no perceptible delay. Light sleep wakes in ~1ms with RAM retained, so recording begins on the same button press that wakes the device.

**Correction (ADR 0011): the tradeoff this ADR named does not exist.** "~1-5mA vs ~10µA" compared a whole board against a bare ESP32-S3 datasheet figure. On this board the VBAT power latch draws ~2.6mA whenever the device is alive at all, in *either* sleep mode, so the real gap is the SoC's ~240µA -- Idle ~2.9mA against a deep-sleep park's ~2.6mA. The decision stands unchanged, but it rests on the instant-capture requirement alone, which was always the only reason it needed. Reaching a genuine low-drain state means opening the latch entirely (ADR 0011's Off tier), not sleeping the CPU harder.

## Considered Options

- **Deep sleep**: best battery life, but ~1-2s boot delay before recording starts. Ruled out because instant capture is a hard requirement. (The battery-life premise was also overstated -- see the correction above.)
- **Always-on (no sleep)**: zero latency, but draws full active current (~80mA) continuously.
