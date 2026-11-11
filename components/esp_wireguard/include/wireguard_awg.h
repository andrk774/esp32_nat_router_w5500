/* AmneziaWG obfuscation layer for esp_wireguard.
 *
 * AmneziaWG keeps WireGuard's cryptography and changes how packets look on the
 * wire, so that DPI doesn't recognise them:
 *   Jc/Jmin/Jmax  random junk datagrams sent before each handshake initiation
 *   I1..I5        "signature" datagrams built from a template, sent before junk
 *   S1..S4        random prefix on initiation/response/cookie/transport packets
 *   H1..H4        message type values (or ranges) replacing 1, 2, 3 and 4
 *
 * Behaviour follows amnezia-vpn/amneziawg-go. Its newer optional extensions
 * (header_protection_key, random_trailers, content_padding_addition) are not
 * implemented.
 *
 * This file has no lwIP or ESP-IDF dependency except wireguard_random_bytes().
 */
#ifndef _WIREGUARD_AWG_H_
#define _WIREGUARD_AWG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WG_AWG_SIGNATURE_PACKETS 5

/* Limits: everything must fit one Ethernet-sized UDP datagram, and S4 has to
 * leave a usable tunnel MTU (1420 at most, 1280 at the least). */
#define WG_AWG_MAX_DATAGRAM      1472
#define WG_AWG_MAX_JUNK_COUNT    128
#define WG_AWG_MAX_S4            140

typedef struct {
    uint32_t min;
    uint32_t max;
} wg_awg_range_t;

typedef struct {
    uint16_t jc;
    uint16_t jmin;
    uint16_t jmax;
    uint16_t s1;    /* initiation prefix */
    uint16_t s2;    /* response prefix */
    uint16_t s3;    /* cookie reply prefix */
    uint16_t s4;    /* transport data prefix */
    wg_awg_range_t h[4];    /* [0]=initiation ... [3]=transport */
    const char *i[WG_AWG_SIGNATURE_PACKETS];    /* NULL or "" = not used; not copied */
} wg_awg_config_t;

/* All obfuscation off, message types 1..4: identical to plain WireGuard. */
void wg_awg_config_init(wg_awg_config_t *cfg);

/* Parses "123" or "100-200". */
bool wg_awg_parse_range(const char *text, wg_awg_range_t *out);

/* NULL if the configuration is usable, otherwise a short English reason. */
const char *wg_awg_validate(const wg_awg_config_t *cfg);

/* Datagram length of a signature template: <b 0xHEX> <r N> <rc N> <rd N> <t>
 * <dz N> (<d> and <ds> carry no data here). 0 = empty, -1 = invalid. */
int wg_awg_signature_len(const char *spec);

/* Writes wg_awg_signature_len(spec) bytes to dst. spec must be valid. */
void wg_awg_signature_render(const char *spec, uint8_t *dst);

/* Random length of one junk datagram: Jmin <= n < Jmax (Jmin if equal). */
uint16_t wg_awg_junk_len(const wg_awg_config_t *cfg);

/* msg_type is 1..4 (MESSAGE_*). cfg may be NULL for plain WireGuard. */
uint16_t wg_awg_prefix_len(const wg_awg_config_t *cfg, uint8_t msg_type);

/* Writes the 4-byte little-endian type field at the start of a message. */
void wg_awg_write_type(const wg_awg_config_t *cfg, void *msg, uint8_t msg_type);

/* Identifies a received datagram. Returns MESSAGE_* (0 = not ours) and sets
 * *prefix to the bytes to skip before the WireGuard message. */
uint8_t wg_awg_classify(const wg_awg_config_t *cfg, const uint8_t *data, size_t len, size_t *prefix);

#ifdef __cplusplus
}
#endif

#endif /* _WIREGUARD_AWG_H_ */
