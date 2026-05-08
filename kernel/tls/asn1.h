#ifndef CUPID_TLS_ASN1_H
#define CUPID_TLS_ASN1_H

#include "../types.h"

/* DER cursor + readers used by the X.509 parser. Strict: every length
 * is bounds-checked against the parent; non-canonical lengths and
 * negative INTEGERs are rejected. The full input must lie in memory -
 * we never copy, we walk. All "body" pointers returned alias the
 * caller's buffer. */

#define ASN1_TAG_BOOLEAN       0x01
#define ASN1_TAG_INTEGER       0x02
#define ASN1_TAG_BIT_STRING    0x03
#define ASN1_TAG_OCTET_STRING  0x04
#define ASN1_TAG_NULL          0x05
#define ASN1_TAG_OID           0x06
#define ASN1_TAG_UTF8          0x0C
#define ASN1_TAG_PRINTABLE     0x13
#define ASN1_TAG_IA5           0x16
#define ASN1_TAG_UTCTIME       0x17
#define ASN1_TAG_GENTIME       0x18
#define ASN1_TAG_SEQUENCE      0x30
#define ASN1_TAG_SET           0x31
#define ASN1_TAG_CTX0          0xA0
#define ASN1_TAG_CTX1          0xA1
#define ASN1_TAG_CTX2          0xA2
#define ASN1_TAG_CTX3          0xA3

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} asn1_cur_t;

void asn1_init(asn1_cur_t *c, const uint8_t *p, uint32_t len);

/* Bytes remaining to be read. */
uint32_t asn1_remaining(const asn1_cur_t *c);

/* Peek the next tag byte. Returns -1 if end. */
int asn1_peek_tag(const asn1_cur_t *c);

/* Read a TLV with the expected tag; sets *body and *body_len to the
 * value range, advances *c past the value. Returns 0 on success, -1
 * on tag mismatch / truncation / non-canonical length. */
int asn1_read_tlv(asn1_cur_t *c, uint8_t expected_tag,
                  const uint8_t **body, uint32_t *body_len);

/* Like asn1_read_tlv, but returns a sub-cursor scoped to the body. */
int asn1_open(asn1_cur_t *c, uint8_t expected_tag, asn1_cur_t *sub);

/* Read whatever TLV is next (any tag). Sets *tag, *body, *body_len.
 * Returns 0 on success, -1 on truncation. */
int asn1_read_any(asn1_cur_t *c, uint8_t *tag,
                  const uint8_t **body, uint32_t *body_len);

/* Skip the next TLV. Returns 0 on success, -1 on parse error. */
int asn1_skip_any(asn1_cur_t *c);

/* Read INTEGER as canonical unsigned big-endian. Strips a single
 * leading 0x00 if it's there only to keep the high bit clear; rejects
 * sign-extension, multi-byte zero pads, and negative values. */
int asn1_read_uint(asn1_cur_t *c,
                   const uint8_t **bytes, uint32_t *len);

/* Read OID body (caller compares against known DER OID byte sequences). */
int asn1_read_oid(asn1_cur_t *c,
                  const uint8_t **oid, uint32_t *oid_len);

/* Read BIT STRING. Rejects nonzero unused-bits byte (we only deal
 * with byte-aligned strings: signatures + SPKI). Returns the body
 * AFTER the unused-bits byte. */
int asn1_read_bit_string(asn1_cur_t *c,
                         const uint8_t **data, uint32_t *data_len);

/* Read OCTET STRING. */
int asn1_read_octet_string(asn1_cur_t *c,
                           const uint8_t **data, uint32_t *data_len);

/* Read either UTCTime (YYMMDDHHMMSSZ) or GeneralizedTime
 * (YYYYMMDDHHMMSSZ) and convert to seconds-since-Unix-epoch. */
int asn1_read_time(asn1_cur_t *c, uint64_t *epoch_seconds);

/* Helper: byte-equal compare against a known OID. */
int asn1_oid_equals(const uint8_t *oid, uint32_t oid_len,
                    const uint8_t *want, uint32_t want_len);

#endif
