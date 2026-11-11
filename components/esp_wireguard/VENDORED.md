Copied from https://github.com/martin-ger/esp_wireguard_idf_5.5.x at commit
d0b401d (v0.9.2, includes the GHSA security fixes) so that AmneziaWG could be added.
Changes: src/wireguard_awg.c and include/wireguard_awg.h are new; packet framing
in src/wireguardif.c and message types in src/wireguard.c go through them.
Without an AmneziaWG configuration the behaviour is unchanged.
