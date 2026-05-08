/* DER reader for X.509. Strict bounds checks + canonical-form
 * enforcement. Bug class to remember: BERserk (lax length parsing) and
 * goto-fail (skipped check). Every length is verified to fit in the
 * parent before we advance the cursor. */

#include "asn1.h"

void asn1_init(asn1_cur_t *c, const uint8_t *p, uint32_t len) {
    c->p   = p;
    c->end = p + len;
}

uint32_t asn1_remaining(const asn1_cur_t *c) {
    return (uint32_t)(c->end - c->p);
}

int asn1_peek_tag(const asn1_cur_t *c) {
    if (c->p >= c->end) return -1;
    return (int)c->p[0];
}

/* Read a DER length field. Returns 0 on success and writes *len_out;
 * advances *pp past the length bytes. Returns -1 on truncation,
 * non-canonical encoding, or length > 0xFFFFFF (we cap at 16 MB which
 * is far more than any cert). */
static int read_len(const uint8_t **pp, const uint8_t *end, uint32_t *len_out) {
    const uint8_t *p = *pp;
    uint32_t len;
    uint32_t n;
    uint32_t i;

    if (p >= end) return -1;
    if ((p[0] & 0x80u) == 0) {
        /* Short form. */
        len = (uint32_t)p[0];
        *len_out = len;
        *pp = p + 1;
        return 0;
    }

    n = (uint32_t)(p[0] & 0x7Fu);
    /* Indefinite form (n == 0) is BER-only, not DER. Reject. */
    if (n == 0u) return -1;
    /* Cap at 4 length bytes -> 32-bit length. */
    if (n > 4u) return -1;
    if ((uint32_t)(end - p) < n + 1u) return -1;

    /* Canonical: forbid using long form when short would suffice. */
    if (p[1] == 0x00u) return -1;

    len = 0;
    for (i = 0; i < n; i++) {
        len = (len << 8) | (uint32_t)p[1 + i];
    }
    /* Reject lengths that overflow our 24-bit cap. */
    if (len > 0xFFFFFFu) return -1;
    /* Canonical DER: long form is only legal when value ≥ 128. */
    if (len < 0x80u) return -1;

    *len_out = len;
    *pp = p + 1 + n;
    return 0;
}

int asn1_read_any(asn1_cur_t *c, uint8_t *tag,
                  const uint8_t **body, uint32_t *body_len) {
    const uint8_t *p = c->p;
    const uint8_t *end = c->end;
    uint32_t len = 0;
    uint8_t  t;

    if (p >= end) return -1;
    t = p[0];
    p++;

    if (read_len(&p, end, &len) != 0) return -1;
    if ((uint32_t)(end - p) < len) return -1;

    *tag = t;
    *body = p;
    *body_len = len;
    c->p = p + len;
    return 0;
}

int asn1_read_tlv(asn1_cur_t *c, uint8_t expected_tag,
                  const uint8_t **body, uint32_t *body_len) {
    asn1_cur_t save = *c;
    uint8_t t = 0;
    if (asn1_read_any(c, &t, body, body_len) != 0) {
        *c = save;
        return -1;
    }
    if (t != expected_tag) {
        *c = save;
        return -1;
    }
    return 0;
}

int asn1_open(asn1_cur_t *c, uint8_t expected_tag, asn1_cur_t *sub) {
    const uint8_t *body = NULL;
    uint32_t       len = 0;
    if (asn1_read_tlv(c, expected_tag, &body, &len) != 0) return -1;
    sub->p   = body;
    sub->end = body + len;
    return 0;
}

int asn1_skip_any(asn1_cur_t *c) {
    uint8_t t = 0;
    const uint8_t *body = NULL;
    uint32_t len = 0;
    return asn1_read_any(c, &t, &body, &len);
}

int asn1_read_uint(asn1_cur_t *c,
                   const uint8_t **bytes, uint32_t *len) {
    const uint8_t *body = NULL;
    uint32_t blen = 0;
    if (asn1_read_tlv(c, ASN1_TAG_INTEGER, &body, &blen) != 0) return -1;
    if (blen == 0u) return -1;
    /* High bit set means negative - reject. */
    if (body[0] & 0x80u) return -1;
    /* Strip a single leading zero added only to keep high bit clear. */
    if (blen >= 2u && body[0] == 0x00u && (body[1] & 0x80u)) {
        body++; blen--;
    } else if (blen >= 2u && body[0] == 0x00u) {
        /* Non-canonical leading zero - reject. */
        return -1;
    }
    *bytes = body;
    *len = blen;
    return 0;
}

int asn1_read_oid(asn1_cur_t *c,
                  const uint8_t **oid, uint32_t *oid_len) {
    return asn1_read_tlv(c, ASN1_TAG_OID, oid, oid_len);
}

int asn1_read_bit_string(asn1_cur_t *c,
                         const uint8_t **data, uint32_t *data_len) {
    const uint8_t *body = NULL;
    uint32_t blen = 0;
    if (asn1_read_tlv(c, ASN1_TAG_BIT_STRING, &body, &blen) != 0) return -1;
    if (blen < 1u) return -1;
    /* Reject any non-byte-aligned bit string - TLS uses only those. */
    if (body[0] != 0x00u) return -1;
    *data = body + 1;
    *data_len = blen - 1u;
    return 0;
}

int asn1_read_octet_string(asn1_cur_t *c,
                           const uint8_t **data, uint32_t *data_len) {
    return asn1_read_tlv(c, ASN1_TAG_OCTET_STRING, data, data_len);
}

int asn1_oid_equals(const uint8_t *oid, uint32_t oid_len,
                    const uint8_t *want, uint32_t want_len) {
    uint32_t i;
    if (oid_len != want_len) return 0;
    for (i = 0; i < oid_len; i++) {
        if (oid[i] != want[i]) return 0;
    }
    return 1;
}

/* Time decoding */

static int dig2(const uint8_t *p, uint32_t *out) {
    if (p[0] < '0' || p[0] > '9') return -1;
    if (p[1] < '0' || p[1] > '9') return -1;
    *out = (uint32_t)(p[0] - '0') * 10u + (uint32_t)(p[1] - '0');
    return 0;
}

static int dig4(const uint8_t *p, uint32_t *out) {
    uint32_t hi = 0, lo = 0;
    if (dig2(p, &hi) != 0) return -1;
    if (dig2(p + 2, &lo) != 0) return -1;
    *out = hi * 100u + lo;
    return 0;
}

/* Days from civil date (Howard Hinnant's algorithm - works for any
 * year past 0000-03-01; we only ever see >= 1970). Output is the
 * count of days since 1970-01-01. */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
    int32_t y2 = y - (m <= 2u ? 1 : 0);
    int32_t era = (y2 >= 0 ? y2 : y2 - 399) / 400;
    uint32_t yoe = (uint32_t)(y2 - era * 400);
    uint32_t doy = (153u * (m + (m > 2u ? -3u : 9u)) + 2u) / 5u + d - 1u;
    uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

int asn1_read_time(asn1_cur_t *c, uint64_t *epoch_seconds) {
    uint8_t t = 0;
    const uint8_t *body = NULL;
    uint32_t blen = 0;
    uint32_t year = 0, mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
    int32_t  days;
    uint64_t secs;

    if (asn1_read_any(c, &t, &body, &blen) != 0) return -1;

    if (t == ASN1_TAG_UTCTIME) {
        /* YYMMDDHHMMSSZ - exactly 13 bytes, last 'Z'. */
        uint32_t yy = 0;
        if (blen != 13u) return -1;
        if (body[12] != 'Z') return -1;
        if (dig2(body + 0, &yy)  != 0) return -1;
        if (dig2(body + 2, &mon) != 0) return -1;
        if (dig2(body + 4, &day) != 0) return -1;
        if (dig2(body + 6, &hh)  != 0) return -1;
        if (dig2(body + 8, &mm)  != 0) return -1;
        if (dig2(body + 10, &ss) != 0) return -1;
        /* RFC 5280 §4.1.2.5.1: yy < 50 => 20yy, else 19yy. */
        year = (yy < 50u) ? (2000u + yy) : (1900u + yy);
    } else if (t == ASN1_TAG_GENTIME) {
        /* YYYYMMDDHHMMSSZ - exactly 15 bytes, last 'Z'. */
        if (blen != 15u) return -1;
        if (body[14] != 'Z') return -1;
        if (dig4(body + 0, &year) != 0) return -1;
        if (dig2(body + 4, &mon)  != 0) return -1;
        if (dig2(body + 6, &day)  != 0) return -1;
        if (dig2(body + 8, &hh)   != 0) return -1;
        if (dig2(body + 10, &mm)  != 0) return -1;
        if (dig2(body + 12, &ss)  != 0) return -1;
    } else {
        return -1;
    }

    if (mon < 1u || mon > 12u) return -1;
    if (day < 1u || day > 31u) return -1;
    if (hh > 23u || mm > 59u || ss > 60u) return -1;

    days = days_from_civil((int32_t)year, mon, day);
    secs = (uint64_t)days * 86400ull
         + (uint64_t)hh * 3600ull
         + (uint64_t)mm * 60ull
         + (uint64_t)ss;
    *epoch_seconds = secs;
    return 0;
}
