#ifndef DNS_H
#define DNS_H

#include "types.h"

/* Resolve A record. Returns 0 on success, negative on failure.
 * ipv4_out receives network byte order (low byte = first octet). */
int dns_resolve(const char *name, uint32_t *ipv4_out);

#endif
