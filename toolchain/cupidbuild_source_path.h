#ifndef CUPID_TOOLCHAIN_BUILD_SOURCE_PATH_H
#define CUPID_TOOLCHAIN_BUILD_SOURCE_PATH_H

#include <stddef.h>

/* Produce the image command's absolute lexical source spelling. Relative
 * requests use the supplied absolute working root. This follows no aliases,
 * observes no file and creates no namespace entry. Input strings are UTF-8;
 * each input and the result may contain at most 8,191 bytes. Input and output
 * buffers must not overlap. Failure clears a supplied output.
 * The caller must retain filesystem observations before using the spelling. */
int cupidbuild_source_path_absolute(const char *root, const char *request,
                                   char *output, size_t capacity);

#endif
