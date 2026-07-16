// Copy to wifi_secrets.h and update. wifi_secrets.h is not committed.
//
//   cp components/sync/wifi_secrets.example.h components/sync/wifi_secrets.h
//
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

// Saved networks, tried by scanning at sync start and joining the strongest one
// actually in range.
#define WIFI_NETWORKS                                                          \
  {                                                                            \
    {"home-ssid", "home-password"}, { "office-ssid", "office-password" }       \
  }

// Per-owner Companion mDNS hostname, resolved at the start of each sync.
#define COMPANION_HOSTNAME "josh-memo"
#define COMPANION_PORT 8080

// Shared bearer token, identical to the Companion's COMPANION_TOKEN.
#define COMPANION_TOKEN "change-me"

#endif
