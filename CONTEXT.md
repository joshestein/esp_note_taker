# ESP32-S3 Voice Note Device -- Glossary

## Capture
A single audio recording session. Begins when the user presses the Record Button and ends when they press it again. Stored as one WAV file on the SD card. Discarded if shorter than 2 seconds.

## Record Button
The BOOT button (GPIO0). Wakes the device from Idle and immediately begins a Capture. A second press ends the Capture.

## Menu Button
The PWR button (GPIO18). Wakes the device from Idle and enters the Menu. Menu features are deferred.

## Idle
The device state when no Capture is in progress. The CPU is in light sleep; GPIO interrupts from either button trigger immediate wake. The e-paper display retains the last drawn image without power.

## Recording State
The device state during an active Capture. Audio is read from the codec and written in chunks to an open WAV file on the SD card.

## Finalizing
The device state after a Capture ends, while the record task patches the WAV header and closes the file on the SD card. Button presses during Finalizing are dropped, not queued -- a press here does not start a new Capture. Prevents accidental stacked recordings on a pocket-worn device and avoids racing the still-open file handle.

## Light Sleep
The ESP32-S3 sleep mode used during Idle. CPU is paused, RAM is retained, GPIO interrupts wake the device in ~1ms. Chosen over deep sleep to allow instant Capture start on button press.

## WAV File
The storage format for each Capture. PCM, 16kHz sample rate, 1 channel (mono), 16-bit depth. Mono because the ES8311 codec has a single mic; the 2-channel manufacturer example was loopback playback, not memo storage. Named by timestamp: `note_YYYYMMDD_HHMMSS.wav`.

## Recording Indicator
An LED held steady for the full duration of a Capture, off otherwise. Lets the wearer confirm a session is active without close inspection. Chosen over haptic/audio cues for low power and simplicity.

## Error Indication
A distinct LED pattern (e.g. blinking, unlike the steady Recording Indicator) signalling a failure the wearer must act on: no SD card at record start, card full, or card removed mid-Capture. The record task aborts to Finalizing (or straight to Idle if the card is gone) on any `fwrite` failure and raises this indication.

## SD Card
The storage medium for WAV files. Required for device operation. Exposed via `sdcard_bsp` from the manufacturer example.

## RTC
The PCF85063 real-time clock (I2C 0x51). Provides timestamps for WAV file naming. Time-setting is deferred to Menu implementation.
