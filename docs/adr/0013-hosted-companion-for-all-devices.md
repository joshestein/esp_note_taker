# One Hosted Companion for All Devices

> **Status: proposed, tentative.** Result of an exploratory design session on 2026-11-16. Nothing is built. ADR 0003 stays in force until this ADR is accepted. If accepted, this ADR supersedes ADR 0003 and replaces the discovery, auth, and 1:1 parts of ADR 0006. The full decision list is in `docs/hosted-companion-plan.md`.

Friends asked for a device. They are not technical: they cannot flash firmware, run a Companion, or keep a computer on at home. So the Operator (the person who runs the Companion, today the project owner) runs one Companion on a rented Linux server. Every device syncs to it over the internet, the Operator's own device included. This reverses ADR 0003, which kept all audio on the LAN.

The privacy cost is accepted and kept small:

- **One custodian.** The Operator can read every Transcript, and the server host has disk access. No outside speech-to-text or LLM vendor sees a memo: transcription runs on the server.
- **Audio is not kept.** The Companion deletes a Capture's audio after it makes the Transcript. The device stays the archive for audio (Synced folder).
- **Text is kept.** The Companion keeps Transcripts, and a Wearer reads them on a web page on the Companion. A lost device loses audio, not text.

A Capture is filed under the device that sent it, not under a person. Each device holds its own token.

There is one system, not two. The Operator's device uses the same path as a friend's device, so the Operator finds faults first.

## Considered Options

- **A Companion app for each friend (the ADR 0008 plan)**: memos stay on the friend's hardware. Each friend must install the app and keep a computer on at home, and `parakeet-mlx` needs Apple Silicon. Rejected: the friends are not technical.
- **A Mac at the Operator's home, behind a tunnel**: audio stays on the Operator's hardware and `parakeet-mlx` stays. Sync and the page stop when the home network or the Mac is down. Rejected.
- **Rented server with a cloud speech-to-text API**: no model to run. A second party sees all audio. Rejected.
- **Two systems (Operator on the LAN, friends on the hosted Companion)**: the Operator keeps the privacy of ADR 0003. The firmware carries two discovery paths and two transports, and the Operator does not use the path friends depend on. Rejected.
- **Companion keeps nothing (processor only)**: least custody. A friend then needs a place outside the Companion to read Transcripts. Rejected in favor of the web page.
- **Companion keeps audio too (full backup)**: a lost device loses nothing. The Operator then holds all friends' audio. Rejected: audio has little use after transcription.

## Consequences

- TLS is necessary. The token and the audio cross public networks. A public DNS name replaces mDNS.
- The 1:1 invariant ends. Capture names are unique for one device only, so the Companion needs a namespace for each device.
- The speech-to-text engine must run on Linux. `parakeet-mlx` cannot.
- The mDNS reason against Docker in ADR 0008 ends.
- The Keyword hand-off in ADR 0012 writes a file on the Operator's Mac. A rented server cannot do that. It needs a new route.
- The web page puts friends' private text on the public internet. It needs an auth gate before any friend's text is on it. The method is not decided.
- The Operator has an operations duty with no end date. If the server stops, devices still record and keep audio, but get no Transcripts.
- Companion downtime loses no data. Sync is retried for each file.
