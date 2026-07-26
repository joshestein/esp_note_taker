# A Power Off Tier, and Corrected Sleep Drain Figures

ADR 0001 and ADR 0007 both reason from drain figures that are wrong for this board. Reading the Waveshare schematic while fixing the park path (see Mechanism below) turned up the reason: **the VBAT power latch costs ~2.6mA to hold, unconditionally, in every state the device can be in while alive.** `BAT_Control` (GP17) drives T2's base through R64 1K, so holding it high draws (3.3V - 0.7V) / 1K ≈ 2.6mA. Nothing about sleeping the CPU touches that.

That collapses the two-tier model's economics and makes a third tier -- a real **Off** -- the only way to get a low-drain state at all.

## Corrected figures

All computed from the schematic and SoC datasheet typicals. **None are measured yet**; measurement is still outstanding (see ADR 0010's "sleep must be measured" consequence).

| State | Latch | VBAT divider | SoC | Total order |
|---|---|---|---|---|
| Idle (light sleep) | 2.6mA | ~10µA | ~240µA | **~2.9mA** |
| Park (deep sleep, latch held) | 2.6mA | ~10µA | ~7µA | **~2.6mA** |
| Off (latch open) | 0 | ~10µA | 0 | **~10-15µA** |

The VBAT divider is R21/R38, 200K+200K straight across VBAT ahead of Q4, so it survives Off. Off also pays the ETA6098's quiescent draw and the PCF85063's battery backup (~220nA), both small.

What this means:

- **ADR 0001's "~1-5mA vs ~10µA" tradeoff does not exist.** The ~1-5mA figure was roughly right by accident; the ~10µA was the ESP32-S3 datasheet number for a bare SoC, which this board is not. Light sleep costs ~240µA more than deep sleep, not ~5mA more. ADR 0001's decision stands on the instant-capture requirement alone, which is the only reason it ever needed.
- **ADR 0007's "~10µA" park is unreachable while the latch is held**, which it must be for either button to wake the device.
- **The park tier saves ~8%** of Idle drain (~240µA of ~2.9mA) in exchange for losing RAM and a 0.4-1s cold boot. That is a poor trade and the park tier is now hard to justify on drain grounds. It is kept for now because it is the only state that preserves wake-to-record while being cheaper than Idle, and because removing a documented state is a separate decision. **Open question:** whether Sleep should be deleted, leaving Idle and Off.

## The Off state

Entered from a **Power off card** in the Menu. `power_off()` drives GP17 low: T2 stops conducting, R63 pulls Q4's gate to VBAT, Q4 turns off, VSYS collapses. Execution stops mid-instruction. Nothing is flushed because nothing is in flight -- Off is reachable only from the Menu, so no Capture is open.

**Only the Menu Button can revive the device.** That pin *is* `BAT_KEY`, wired to pull Q4's gate low through D9 with no firmware involved. The Record Button is GP0, a plain GPIO with no power path, so it cannot bring the rail up from Off at any hold length. This is a hardware fact, not a policy: wake-to-record is a **Park** property and can never be an **Off** property.

Waveshare's own `12_RTC_Sleep_Test` treats a `BAT_KEY` wake from deep sleep as a power-off, i.e. it uses the key as an on/off toggle. We do not: our Menu Button wake goes to Idle, and Off is reached deliberately from a card. The card is the same argument ADR 0007 made for moving park off a hold -- a pocket cannot press a card, and Off is the more expensive accident of the two.

**On USB, Power off does nothing.** VBUS feeds VSYS through D4 regardless of GP17, so `power_off()` returns instead of dying. It restores the latch (otherwise a later unplug would cut power at an arbitrary moment) and the device reports `Unplug USB to power off` and returns to Idle. Reporting the no-op beats silently appearing to work.

## Mechanism (for the record)

Two defects were fixed alongside this, both in `enter_deep_sleep()`:

1. **The park never lasted.** `esp_pm` arms a timer wake-up before every light sleep and never disarms it, and `s_config.wakeup_triggers` is global while the `esp_sleep_enable_*` calls only OR bits in. So `esp_deep_sleep_start()` inherited a millisecond-scale timer from the last light-sleep tick and woke immediately, reading as a spontaneous reboot. Fixed with `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`, preceded by `esp_pm_configure(light_sleep_enable = false)` so the idle task cannot re-arm it.
2. **The park powered the board off on battery.** GP17 was not held, so the pad reset on the way into deep sleep and the latch opened -- the accidental Off this ADR now makes deliberate. Fixed with `gpio_hold_en(VBAT_PWR_PIN)` before sleeping (GPIO17 is RTC-capable, so this routes to `rtc_gpio_hold_en` and survives deep sleep) and `gpio_hold_dis` in `init_power()` once the output is already driving high.

A wake press is also drained before `button_init()`, because `iot_button` was reading its release as a fresh gesture: a Menu-Button wake landed in the Menu instead of Idle, and a Record-Button wake could latch a press that stopped the Capture the wake had just started.

## Considered Options

- **Keep two tiers, accept ~2.6mA as the floor**: no new card, no new state. Ruled out because ~2.6mA is not a storage state -- it is the thing ADR 0007 was written to escape, and it fails at that by two orders of magnitude.
- **Make a Menu-Button wake from Park mean Off, as Waveshare do**: no new card, and the power key behaves as users of this board expect. Ruled out because it overloads one gesture with two meanings depending on invisible state, and it removes wake-to-idle from Park, leaving Park with only the clipped-audio wake.
- **Reduce the latch's 2.6mA in firmware**: nothing to reduce. The current is set by R64 and T2's base-emitter drop; drive strength limits capability, not steady-state current. A hardware change is out of scope.
- **Off as a long hold rather than a card**: cheaper gesture, but this is now the device's most expensive accident (only the power key recovers it), so it wants *more* deliberation than Park, not less. Same reasoning ADR 0007's amendment used.
