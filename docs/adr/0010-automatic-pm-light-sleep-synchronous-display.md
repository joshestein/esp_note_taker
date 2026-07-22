# Automatic PM Light Sleep, Enabled by a Synchronous Display Port

ADR 0001 chose **light sleep** for Idle over deep sleep; this ADR decides *how* it is entered and clears the one thing that was silently preventing it. Idle light sleep is driven by **automatic power management** (`esp_pm` + tickless idle), not an explicit `esp_light_sleep_start()` call -- and the enabling work is a **synchronous rewrite of the display port**, because the Waveshare-derived LVGL tick timer and background task kept the CPU awake and would have made the PM config a no-op.

## Mechanism: automatic, not manual

Light sleep is left to the framework: `CONFIG_PM_ENABLE`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, and one `esp_pm_configure()` call (`light_sleep_enable = true`). When every task is blocked and no PM lock is held, tickless idle enters light sleep on its own; a GPIO edge wakes it in ~1ms.

This fits the code that already exists. The main loop blocks forever on `app_events_wait()` (`portMAX_DELAY`). The buttons are created with `enable_power_save = true`, which makes `iot_button` **stop its poll timer** when no press is pending and arm `gpio_wakeup_enable` + `esp_sleep_enable_gpio_wakeup()` -- exactly the light-sleep wake path. Nothing in `main.c` has to decide *when* to sleep: the PM locks already encode when *not* to. The enabled I2S channel holds an APB-max lock for the duration of a Capture (mic must run); Wi-Fi holds one across a Sync (radio on). Both drop their lock when the state ends, so Recording and Syncing never sleep and Idle always can, with **no per-state gating**.

A manual `esp_light_sleep_start()` was rejected: it would mean stopping and restarting the `iot_button` timer by hand, picking a point in the loop to sleep, and racing the button wake ISR -- reintroducing the bookkeeping that `enable_power_save` already does correctly.

## No DFS: `min == max == 160 MHz`

`esp_pm_configure` fixes the CPU frequency (`min_freq_mhz == max_freq_mhz`); dynamic frequency scaling is off. The idle-power win comes entirely from light sleep (CPU + RAM paused). DFS only helps during *active* work, and every active state here is I/O-bound -- Recording waits on I2S DMA and SD, Sync waits on the radio, paints wait on the e-paper busy line -- so scaling buys almost nothing while adding UART-log corruption and peripheral-clock edge cases.

160 MHz specifically because it is already `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`: fixing min==max there means PM adds light sleep without ever re-clocking the CPU or APB from the frequency every driver was brought up and hardware-validated at. 240 would raise active draw and heat for throughput we do not need; 80 would slow boot/init, which on a Record-Button wake from Park is audio clipped off the front of the memo (ADR 0007), for a small WFI saving during the rare, brief Recording state.

`esp_pm_configure()` is called once after the init block and before the main loop -- kept off the audio-critical wake path, consistent with why Wi-Fi init is lazy and the display trails the Capture start.

## The enabling refactor: synchronous display port

The Waveshare LVGL example (and the port lifted from it) runs two things forever: a **2 ms periodic `esp_timer`** feeding `lv_tick_inc`, and a **background `lvgl_task`** looping `lv_timer_handler()` on a 1-500 ms delay. Either one defeats light sleep: the 2 ms tick alone forces ~500 wake-ups per second, so the CPU bounces in and out of sleep continuously and the idle-current win evaporates. With that timer running, the PM config above is a no-op.

The display is therefore driven **synchronously**. There is no free-running tick and no background LVGL task. Each `display_show_*` loads its screen and pumps `lv_timer_handler()` in a bounded loop until `flush_cb` signals the panel write is done, then returns; between paints LVGL is fully quiescent and the CPU is free to light-sleep indefinitely. LVGL's tick is **pull-based** (`lv_tick_set_cb` returning `esp_timer_get_time()`), so time is read only while a paint is running, never on a timer.

This is sound because the UI never animates: the e-paper holds its image with zero CPU, screens are painted only on state transitions (`main.c` already drives every paint), and nothing on screen -- battery ring, menu cards, messages -- moves on its own. LVGL's asynchronous tick+timer model exists for animated, self-refreshing displays; this device has neither. With the background task gone, LVGL has a single owner (the main task), so the `lvgl_mux` guarding it is dropped as well.

## Consequences

- **GPIO17 (the VBAT power-hold latch) must survive light sleep.** Digital output state is auto-retained in light sleep -- unlike deep sleep, no `gpio_hold` / `rtc_gpio_hold` is needed (those are the deep-sleep RTC-domain mechanism the manufacturer's `12_RTC_Sleep_Test` uses). But automatic tickless sleep offers no sleep-call to wrap with a belt-and-suspenders hold, and if GPIO17 drops even briefly the board cuts its own battery rail and looks bricked (USB-only recovery). Acceptance is gated on a hardware test: unplug USB, let Idle enter light sleep, confirm the device stays alive and wakes on both buttons.
- **Sleep must be measured, not assumed.** `app_events_wait()` blocking looks identical whether the chip light-sleeps or the CPU spins. Verify with `esp_pm_dump_locks()` (no lock held in Idle) and an inline battery-current reading (Idle should drop toward the ~1-2 mA of ADR 0001).

## Considered Options

- **Manual `esp_light_sleep_start()` in the Idle branch**: explicit control over when to sleep, but must hand-manage the `iot_button` poll timer and race the wake ISR. Ruled out -- `enable_power_save` already does this, and automatic sleep needs no `main.c` changes.
- **DFS on (`min < max`)**: lower active draw in principle, but every active state is I/O-bound so the gain is near zero, and it adds UART corruption and peripheral re-clocking. Ruled out.
- **Keep the async LVGL port, start/stop the tick timer and suspend the task around each paint**: preserves the lifted example, but must detect "flush done" to know when to suspend and carries two moving parts for a UI that never animates. Ruled out in favor of the synchronous port, which removes the timer and task outright.
