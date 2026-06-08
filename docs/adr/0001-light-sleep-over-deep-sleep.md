# Light Sleep Instead of Deep Sleep for Idle Mode

The manufacturer example uses deep sleep (full CPU off, ~1-2s boot on wake). We use light sleep instead because the core UX requirement is that pressing the Record Button immediately starts a Capture -- no perceptible delay. Light sleep wakes in ~1ms with RAM retained, so recording begins on the same button press that wakes the device. The tradeoff is higher idle current (~1-5mA vs ~10µA), but acceptable given the use case.

## Considered Options

- **Deep sleep**: best battery life, but ~1-2s boot delay before recording starts. Ruled out because instant capture is a hard requirement.
- **Always-on (no sleep)**: zero latency, but draws full active current (~80mA) continuously.
