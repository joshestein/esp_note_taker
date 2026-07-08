# Route Every Capture Stop Through the Record Task's Exit

Every way a Capture can end -- the wearer pressing Record again, a mid-record codec-read or `fwrite` error, a failed codec start, or the record task failing to allocate its buffer -- converges on a single path: the `record_task` runs its teardown, sets `CAPTURE_ENDED_BIT`, and exits. The main loop, woken by that bit, is what transitions RECORDING -> FINALISING and patches/closes (or deletes) the file. Main never drives the transition itself, and the record button while RECORDING does not either; it only requests a stop.

## Mechanism

The record button and the end-of-capture signal share **one** FreeRTOS event group (`button_group`), with `CAPTURE_ENDED_BIT` a bit that no button sets -- the record task raises it on exit. `main` blocks in a single `xEventGroupWaitBits` on `RECORD_BUTTON_BIT | POWER_BUTTON_BIT | CAPTURE_ENDED_BIT`.

A stop request and the actual stop are decoupled:

- **Requesting a stop** is just `is_recording = false`. The Record button while RECORDING does exactly this and nothing else. The record task's `while (is_recording)` loop sees it and falls out.
- **Enacting the stop** is the record task's job. After the loop (or after an error `break`, a skipped loop on start failure, or a skipped body on malloc failure) it stops the codec, records the outcome in `capture_ok`, gives `s_mutex`, sets `CAPTURE_ENDED_BIT`, and deletes itself.
- **Finalizing** happens only in `main`, only after it sees `CAPTURE_ENDED_BIT` and takes `s_mutex` (which the now-exited task has released), guaranteeing the task is done touching the file before the header is patched and the file is closed or removed.

`capture_ok` is the one channel for "did this Capture produce a good file." It defaults **false** (set in `main` before `xTaskCreate`, so a task that never runs leaves it false and the file is deleted) and is set true by the task only on a clean loop exit. The record task is its sole writer, so there is no cross-task write race on it.

## Why one path

- **Main can't wait on two event groups.** `xEventGroupWaitBits` takes a single group. Rather than add a second sync primitive (a second group, a queue, a task notification) for "recording finished," the finished signal reuses the button group as a non-button bit. One wait covers buttons and end-of-capture.
- **Single owner of the codec and file.** `record_start`/`record_stop` (codec open/close) and every `wav_write` live inside the record task. If `main` also drove the FINALISING transition on the button press, it could close the WAV or reopen the codec while the task was still mid-read or mid-write -- a read-after-close / double-close race. Making the task the only thing that ends itself removes that race by construction; `main` waits on the mutex before touching the file.
- **Every failure mode exits the same way.** User stop, codec-read error, `fwrite` error, codec-start failure, and buffer-malloc failure all reach the same teardown. There is no separate abort path to keep correct, and FINALISING's delete-vs-save decision reads one flag (`capture_ok`) regardless of how the Capture ended.

## Considered Options

- **Main sets FINALISING directly on the Record button press** (task just stops writing): simplest to read, but reintroduces the race -- main would finalize/close the file while the record task might still be inside a `wav_write`. Rejected; the mutex handshake needs the task to be the one that signals completion.
- **A dedicated second event group / queue / task notification for "capture ended"**: cleaner separation of button events from lifecycle events, but adds a sync primitive and a second thing for `main` to wait on for no functional gain at this scale. Reusing the button group with a reserved bit is enough.
- **Poll `is_recording` / a "done" flag from main**: no extra primitive, but spins or sleeps on a timer instead of blocking, wasting the light-sleep-friendly event-wait already in place. Rejected on power and simplicity.
