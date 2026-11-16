// Copy to wifi_secrets.h and update. wifi_secrets.h is not committed.
//
//   cp components/sync/wifi_secrets.example.h components/sync/wifi_secrets.h
//
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

// PEM CA of the enterprise network's RADIUS server (Keychain Access, from the
// cert macOS showed on first join). Keep the trailing \n on every line.
#define OFFICE_CA_PEM                                                          \
  "-----BEGIN CERTIFICATE-----\n"                                              \
  "MIIB...\n"                                                                  \
  "-----END CERTIFICATE-----\n"

// Saved networks, tried by scanning at sync start and joining the strongest one
// actually in range.
// Use designated initializers: positional ones fail the build
// (-Wmissing-field-initializers). For 802.1X set .username and .ca_cert.
#define WIFI_NETWORKS                                                          \
  {                                                                            \
    {.ssid = "home-ssid", .password = "home-password"},                        \
    {.ssid = "office-ssid",                                                    \
     .password = "account-password",                                           \
     .username = "account-username",                                           \
     .ca_cert = OFFICE_CA_PEM},                                                \
  }

// Per-owner Companion mDNS hostname, resolved at the start of each sync.
#define COMPANION_HOSTNAME "josh-memo"
#define COMPANION_PORT 8080

// Shared bearer token, identical to the Companion's COMPANION_TOKEN.
#define COMPANION_TOKEN "change-me"

#endif
