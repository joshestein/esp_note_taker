// Copy to wifi_secrets.h and update. wifi_secrets.h is not committed.
//
//   cp components/sync/wifi_secrets.example.h components/sync/wifi_secrets.h
//
#ifndef WIFI_SECRETS_H
#define WIFI_SECRETS_H

// CA that signed the enterprise network's RADIUS server cert, as PEM. Export it
// from Keychain Access (the cert macOS showed on first join) or from
// `openssl s_client`. Keep the trailing \n on every line.
#define OFFICE_CA_PEM                                                          \
  "-----BEGIN CERTIFICATE-----\n"                                              \
  "MIIB...\n"                                                                  \
  "-----END CERTIFICATE-----\n"

// Saved networks, tried by scanning at sync start and joining the strongest one
// actually in range.
// Designated initializers: the build treats missing fields as an error
// (-Wmissing-field-initializers), and the designated form leaves them unset
// (NULL) on purpose. Set .username only for WPA2/3-Enterprise, and then
// .ca_cert is required: the device refuses to join without it.
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
