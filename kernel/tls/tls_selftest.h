#ifndef CUPID_TLS_SELFTEST_H
#define CUPID_TLS_SELFTEST_H

/* Run all TLS crypto self-tests. Calls panic on any vector mismatch.
 * Cheap (microseconds) - safe to run unconditionally at boot.*/
void tls_selftest_run(void);

#endif
