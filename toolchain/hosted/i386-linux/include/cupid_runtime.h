#ifndef CUPID_HOSTED_I386_RUNTIME_H
#define CUPID_HOSTED_I386_RUNTIME_H

/* Call on the original thread before creating concurrent heap users. Once
 * enabled, malloc/calloc/realloc/free serialize their shared heap metadata.
 * This does not make the other hosted runtime services thread-safe. */
void cupid_runtime_heap_enable_threads(void);

#endif
