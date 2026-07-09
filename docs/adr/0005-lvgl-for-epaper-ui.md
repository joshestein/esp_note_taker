# LVGL as the E-Paper Rendering Stack

On-screen UI is rendered with **LVGL v9.3** driving the Waveshare `epaper_driver_bsp`, wrapped behind a C-API `display_bsp` component (mirroring `audio_bsp` / `sdcard_bsp`). The first screen built on it is the **Recording Screen** (filled circle + "recording"), but the stack is chosen for the whole **Menu** UI that follows.

## Decision

The raw `epaper_driver_bsp` (a C++ class lifted from the manufacturer example) exposes only pixel writes (`EPD_DrawColorPixel`) plus full/partial refresh -- no fonts, shapes, or bitmap blitting. Every Waveshare example for this board that draws text or icons does so through LVGL. Rather than hand-roll a glyph/bitmap layer, we adopt LVGL now because the dynamic Menu (Recordings list, Sync progress, Storage) is expected soon and will need retained-mode widgets, fonts, and layout. Standing the rendering stack up once, for the single static Recording Screen, avoids building a throwaway bitmap path and then replacing it.

LVGL, its C++ glue, the LVGL task, and the LVGL mutex are all encapsulated inside `display_bsp`; no LVGL or C++ types leak into `main.c`, which stays C and calls `display_show_recording()` / `display_show_idle()` on state transitions. The BSP takes the LVGL lock internally.

**v9.3, the latest line:** the board ships a v9 example (`10_LVGL_V9_Test`) with the flush/register glue already ported to v9's display API (`lv_display_create`, `lv_display_set_flush_cb`, `lv_display_set_buffers`), and the underlying `epaper_driver_bsp.cpp` is byte-identical to the v8 example's. So v9 costs nothing extra over v8 to stand up, and keeps us on the current maintained LVGL line for the Menu work ahead. The v9 glue is lifted from example 10.

## Consequences

- **Refresh model:** the Idle<->Recording flip uses **partial refresh** (~0.3s, no flash) for a quiet, immediate transition; a **full refresh** runs once at boot to establish the partial base image and clear power-off ghosting. Occasional full refreshes clear accumulated partial-refresh ghosting later.
- **Display is non-fatal and trailing:** a `display_init` or paint failure never blocks or aborts a Capture (the LED **Recording Indicator** is the primary tell). Paints trail the audio action: the record task starts first, then the screen paints; the WAV closes first, then Idle paints.
- **Light-sleep interaction (open):** LVGL runs a periodic tick timer and a background handler task. When light sleep in Idle (ADR 0001) is implemented, the tick timer and LVGL task must be quiesced before sleeping and resumed on wake, or they will prevent the CPU from staying asleep. This is the main cost of choosing LVGL over a static bitmap and is deferred with the light-sleep work.

## Considered Options

- **Hand-rolled 1-bit bitmap blit:** bake the static Recording Screen as a 1-bit C array and push it straight to the driver buffer. Far leaner (no LVGL task, no tick timer, no PSRAM double-buffers) and friendlier to the light-sleep model. Rejected because the Menu UI is imminent and dynamic; the bitmap path would be thrown away and a second rendering stack introduced anyway.
- **LVGL v8.4:** the `audio_bsp` example lineage is on v8. Rejected because the board also ships a v9 example whose glue is already ported and whose driver is byte-identical, so v8 offers no simplicity gain and would put us on the older line.
- **Convert `main` to C++ and call LVGL directly** (as the Waveshare examples do): breaks the established C-BSP pattern and spreads UI concerns into the state machine. Rejected in favor of the `display_bsp` C wrapper.
