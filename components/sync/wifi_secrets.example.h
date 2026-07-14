// Copy to wifi_secrets.h and update. wifi_secrets.h is not committed.
//
//   cp components/sync/wifi_secrets.example.h components/sync/wifi_secrets.h
//
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Per-owner Companion mDNS hostname, resolved at the start of each sync.
#define COMPANION_HOSTNAME "josh-memo"
#define COMPANION_PORT 8080

// Shared bearer token, identical to the Companion's COMPANION_TOKEN.
#define COMPANION_TOKEN "change-me"

#endif
