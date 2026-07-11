# Sync Wire Protocol: How the Device and Companion Speak

ADR 0003 chose the *shape* of sync (device pushes to a paired computer on the LAN, device is HTTP client, files kept as a durable archive). This ADR records *how* the two actually talk: the addressing, auth, transfer, and time-set mechanics. The concrete request/response contract lives in `docs/sync-protocol.md`; this file records the reasoning behind the non-obvious choices.

The device stays a pure HTTP/1.1 client. A sync is a dedicated FreeRTOS task that runs `wifi connect -> mDNS resolve -> upload all unsynced (oldest-first) -> download missing transcripts -> wifi stop`, with bounded connect/resolve timeouts and a clean bail (radio off, Error Indication) on any failure. It is deliberately **not atomic**: a resumable sequence of independent per-file commits, idempotent by filename.

## The decisions and why

- **Addressing: mDNS, per-owner hostname.** The Companion advertises a per-owner `.local` name (e.g. `josh-memo.local`) that each device hardcodes in `wifi_secrets.h`. Chosen over a hardcoded IP (breaks when DHCP moves the address; a wearable with a 200x200 e-paper can't be re-typed) and over full DNS-SD service browsing with a pairing token (correct for a real fleet, overkill for a few hand-configured devices). With several devices on one shared Wi-Fi, uniqueness comes from distinct per-owner hostnames; all a household's devices carry identical Wi-Fi creds and differ only in the target hostname. Hostname -> service-browse is an additive change later.

- **Auth: shared bearer token, plaintext HTTP (Tier 1).** One hardcoded secret both sides hold, sent as `Authorization: Bearer <token>`; the Companion 401s anything without it. This closes the two holes that actually bite on an untrusted LAN: other machines POSTing junk Captures, and other machines scraping Transcripts (the exact privacy leak ADR 0003 exists to prevent). The wire stays cleartext, so a passive sniffer already on the network could still capture the audio during the seconds-long manual sync -- an accepted, narrow risk. TLS (encrypt the wire + verify the Companion via a pinned cert) is Tier 2, deferred, and additive (`http://` -> `https://` + cert).

- **Time-set: the Companion's HTTP `Date` header, not SNTP.** Every HTTP response already carries a standard `Date` header, and the Companion's laptop clock is already trustworthy. Reading it off a response the device is already making sets the RTC with zero extra protocol and, crucially, **no internet dependency** -- an SNTP client against `pool.ntp.org` would re-introduce a cloud dependency into a system designed to work on a LAN with no internet. Deferred with the rest of timestamp naming (v1 uses sequence filenames, so nothing consumes the clock yet).

- **Upload: raw-body POST, streamed from SD.** One WAV per request, filename in the URL path (`POST /captures/note_NNNN.wav`), body = raw WAV bytes. No multipart -- there is no multi-field form to justify boundary generation/parsing for a single file. The body is streamed from the SD card in small chunks with a known `Content-Length` (the file size), so RAM stays flat regardless of memo length and chunked transfer-encoding is avoided. Only a 200 (file flushed/fsync'd on the Companion) moves the Capture into `/sdcard/synced/`.

- **Download: stateless presence-diff, atomic write.** `GET /transcripts` returns a bare array of ready stems; the device diffs that against the `.txt` files present in `/sdcard/transcripts/` and fetches only what it lacks. The Companion tracks **nothing** about the device -- so a second or reflashed device just works. Each `.txt` is streamed to a `.part` temp file and renamed only after a clean 200-to-EOF, so a dropped download can never leave a truncated `.txt` that the presence-diff would mistake for "already have."

## Considered Options

- **Hardcoded IP for addressing** -- simplest, but breaks the first time DHCP moves the Companion; no device-side way to fix it. **DNS-SD browsing with a pairing token** -- robust for a fleet and folds auth in, but real browsing code both sides for three known devices. Picked plain mDNS hostname as the middle.
- **No auth (Tier 0)** -- only defensible on a fully trusted LAN, which we can't assume. **TLS now (Tier 2)** -- closes the sniffing hole too, but drags cert management + handshake cost onto a battery device for a threat far narrower than an open listener. Picked the token (Tier 1) and deferred TLS.
- **SNTP time client** -- "real" NTP, but adds a protocol and an internet dependency that contradicts the pure-LAN design. Rejected for the free `Date` header.
- **multipart/form-data upload** -- familiar, but pure overhead for one file per request. Rejected for raw body.
- **Server tracks per-device sync state** -- would let the Companion send only what's new, but adds device identity/state to a component we want dumb and stateless. Rejected for the local presence-diff.
- **Atomic/transactional sync** -- rejected: independent per-file commits make an interrupted sync resume for free, with no index to corrupt.

## Consequences

- Capture filenames (`note_NNNN`) are per-device-unique only, so the protocol assumes **one Companion serves one device**; per-owner pairing preserves this. Breaking 1:1 (shared laptop, reset counter) risks upload clobber; the retrofit is a per-device filename prefix. See `CONTEXT.md` "Companion discovery".
- Sync brings the project's first `nvs_flash_init` + `esp_netif`/event-loop bring-up (Wi-Fi requires NVS), which had been deliberately avoided until now.
