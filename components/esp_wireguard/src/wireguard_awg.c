/* AmneziaWG obfuscation layer - see wireguard_awg.h */

#include "wireguard_awg.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wireguard-platform.h"

/* Values match MESSAGE_* in wireguard.h */
#define MSG_INITIATION  1
#define MSG_RESPONSE    2
#define MSG_COOKIE      3
#define MSG_TRANSPORT   4

/* WireGuard message sizes */
#define SIZE_INITIATION 148
#define SIZE_RESPONSE   92
#define SIZE_COOKIE     64
#define SIZE_TRANSPORT  32  /* header + auth tag: the shortest transport message */

void wg_awg_config_init(wg_awg_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    for (uint32_t n = 0; n < 4; n++) {
        cfg->h[n].min = n + 1;
        cfg->h[n].max = n + 1;
    }
}

static bool parse_u32(const char *s, char **end, uint32_t *out)
{
    if (!isdigit((unsigned char)*s)) {
        return false;
    }
    unsigned long long v = strtoull(s, end, 10);
    if (v > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static const char *skip_space(const char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

bool wg_awg_parse_range(const char *text, wg_awg_range_t *out)
{
    char *end;
    uint32_t lo, hi;

    if (text == NULL || !parse_u32(skip_space(text), &end, &lo)) {
        return false;
    }
    hi = lo;
    const char *p = skip_space(end);
    if (*p == '-') {
        if (!parse_u32(skip_space(p + 1), &end, &hi)) {
            return false;
        }
        p = skip_space(end);
    }
    if (*p != '\0' || lo > hi) {
        return false;
    }
    out->min = lo;
    out->max = hi;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Walks a signature template. With dst == NULL only measures and validates. */
static int signature_walk(const char *spec, uint8_t *dst)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int total = 0;
    const char *p = spec;

    while ((p = strchr(p, '<')) != NULL) {
        const char *close = strchr(p, '>');
        if (close == NULL) {
            return -1;
        }
        const char *tag = skip_space(p + 1);
        const char *tag_end = tag;
        while (tag_end < close && !isspace((unsigned char)*tag_end)) {
            tag_end++;
        }
        size_t tag_len = (size_t)(tag_end - tag);
        const char *val = tag_end;
        while (val < close && isspace((unsigned char)*val)) {
            val++;
        }
        const char *val_end = close;
        while (val_end > val && isspace((unsigned char)val_end[-1])) {
            val_end--;
        }
        size_t val_len = (size_t)(val_end - val);
        int n = 0;

        if (tag_len == 1 && tag[0] == 'b') {
            /* <b 0xHEX>: fixed bytes */
            if (val_len < 2 || val[0] != '0' || (val[1] != 'x' && val[1] != 'X')) {
                return -1;
            }
            val += 2;
            val_len -= 2;
            if (val_len == 0 || (val_len % 2) != 0) {
                return -1;
            }
            n = (int)(val_len / 2);
            for (int i = 0; i < n; i++) {
                int hi = hex_value(val[2 * i]);
                int lo = hex_value(val[2 * i + 1]);
                if (hi < 0 || lo < 0) {
                    return -1;
                }
                if (dst != NULL) {
                    dst[total + i] = (uint8_t)((hi << 4) | lo);
                }
            }
        } else if (tag_len == 1 && tag[0] == 't') {
            /* <t>: Unix time, big endian */
            n = 4;
            if (dst != NULL) {
                uint32_t now = (uint32_t)time(NULL);
                dst[total + 0] = (uint8_t)(now >> 24);
                dst[total + 1] = (uint8_t)(now >> 16);
                dst[total + 2] = (uint8_t)(now >> 8);
                dst[total + 3] = (uint8_t)now;
            }
        } else if ((tag_len == 1 && tag[0] == 'd') || (tag_len == 2 && tag[0] == 'd' && tag[1] == 's')) {
            /* <d>, <ds>: payload, which signature packets don't have */
            n = 0;
        } else if ((tag_len == 1 && tag[0] == 'r') ||
                   (tag_len == 2 && tag[0] == 'r' && (tag[1] == 'c' || tag[1] == 'd')) ||
                   (tag_len == 2 && tag[0] == 'd' && tag[1] == 'z')) {
            /* <r N> random, <rc N> letters, <rd N> digits, <dz N> payload size (0) */
            if (val_len == 0 || val_len > 5) {
                return -1;
            }
            for (size_t i = 0; i < val_len; i++) {
                if (!isdigit((unsigned char)val[i])) {
                    return -1;
                }
                n = n * 10 + (val[i] - '0');
            }
            if (n == 0) {
                return -1;
            }
            if (dst != NULL) {
                uint8_t *out = dst + total;
                if (tag[0] == 'd') {
                    memset(out, 0, (size_t)n);
                } else {
                    wireguard_random_bytes(out, (size_t)n);
                    if (tag_len == 2 && tag[1] == 'c') {
                        for (int i = 0; i < n; i++) out[i] = (uint8_t)letters[out[i] % 52];
                    } else if (tag_len == 2 && tag[1] == 'd') {
                        for (int i = 0; i < n; i++) out[i] = (uint8_t)('0' + out[i] % 10);
                    }
                }
            }
        } else {
            return -1;
        }

        total += n;
        if (total > WG_AWG_MAX_DATAGRAM) {
            return -1;
        }
        p = close + 1;
    }
    return total;
}

int wg_awg_signature_len(const char *spec)
{
    return (spec == NULL) ? 0 : signature_walk(spec, NULL);
}

void wg_awg_signature_render(const char *spec, uint8_t *dst)
{
    signature_walk(spec, dst);
}

const char *wg_awg_validate(const wg_awg_config_t *cfg)
{
    if (cfg->jc > WG_AWG_MAX_JUNK_COUNT) {
        return "Jc is too large (max 128)";
    }
    if (cfg->jc > 0) {
        if (cfg->jmin > cfg->jmax) {
            return "Jmin is larger than Jmax";
        }
        if (cfg->jmax > WG_AWG_MAX_DATAGRAM) {
            return "Jmax is too large (max 1472)";
        }
    }
    if (cfg->s1 + SIZE_INITIATION > WG_AWG_MAX_DATAGRAM) {
        return "S1 is too large";
    }
    if (cfg->s2 + SIZE_RESPONSE > WG_AWG_MAX_DATAGRAM) {
        return "S2 is too large";
    }
    if (cfg->s3 + SIZE_COOKIE > WG_AWG_MAX_DATAGRAM) {
        return "S3 is too large";
    }
    if (cfg->s4 > WG_AWG_MAX_S4) {
        return "S4 is too large (max 140)";
    }
    for (int a = 0; a < 4; a++) {
        if (cfg->h[a].min == 0 || cfg->h[a].min > cfg->h[a].max) {
            return "H1-H4 must be non-zero values or ascending ranges";
        }
        for (int b = a + 1; b < 4; b++) {
            if (cfg->h[a].min <= cfg->h[b].max && cfg->h[b].min <= cfg->h[a].max) {
                return "H1-H4 must not overlap";
            }
        }
    }
    for (int n = 0; n < WG_AWG_SIGNATURE_PACKETS; n++) {
        if (wg_awg_signature_len(cfg->i[n]) < 0) {
            return "I1-I5 contain an invalid or oversized template";
        }
    }
    return NULL;
}

static uint32_t random_u32(void)
{
    uint32_t v;
    wireguard_random_bytes(&v, sizeof(v));
    return v;
}

uint16_t wg_awg_junk_len(const wg_awg_config_t *cfg)
{
    if (cfg->jmax <= cfg->jmin) {
        return cfg->jmin;
    }
    return (uint16_t)(cfg->jmin + random_u32() % (uint32_t)(cfg->jmax - cfg->jmin));
}

uint16_t wg_awg_prefix_len(const wg_awg_config_t *cfg, uint8_t msg_type)
{
    if (cfg == NULL) {
        return 0;
    }
    switch (msg_type) {
        case MSG_INITIATION: return cfg->s1;
        case MSG_RESPONSE:   return cfg->s2;
        case MSG_COOKIE:     return cfg->s3;
        case MSG_TRANSPORT:  return cfg->s4;
        default:             return 0;
    }
}

void wg_awg_write_type(const wg_awg_config_t *cfg, void *msg, uint8_t msg_type)
{
    uint32_t value = msg_type;

    if (cfg != NULL && msg_type >= MSG_INITIATION && msg_type <= MSG_TRANSPORT) {
        const wg_awg_range_t *r = &cfg->h[msg_type - 1];
        value = r->min;
        if (r->max > r->min) {
            value += (uint32_t)(random_u32() % ((uint64_t)r->max - r->min + 1));
        }
    }
    uint8_t *out = (uint8_t *)msg;
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static bool type_matches(const wg_awg_range_t *r, const uint8_t *data)
{
    uint32_t value = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    return value >= r->min && value <= r->max;
}

uint8_t wg_awg_classify(const wg_awg_config_t *cfg, const uint8_t *data, size_t len, size_t *prefix)
{
    /* Same order and exact-size rules as amneziawg-go's
     * DeterminePacketTypeAndPadding(). */
    if (len == (size_t)cfg->s1 + SIZE_INITIATION && type_matches(&cfg->h[0], data + cfg->s1)) {
        *prefix = cfg->s1;
        return MSG_INITIATION;
    }
    if (len == (size_t)cfg->s2 + SIZE_RESPONSE && type_matches(&cfg->h[1], data + cfg->s2)) {
        *prefix = cfg->s2;
        return MSG_RESPONSE;
    }
    if (len == (size_t)cfg->s3 + SIZE_COOKIE && type_matches(&cfg->h[2], data + cfg->s3)) {
        *prefix = cfg->s3;
        return MSG_COOKIE;
    }
    if (len >= (size_t)cfg->s4 + SIZE_TRANSPORT && type_matches(&cfg->h[3], data + cfg->s4)) {
        *prefix = cfg->s4;
        return MSG_TRANSPORT;
    }
    *prefix = 0;
    return 0;
}
