#include "cupidbuild_host.h"
#include <stdio.h>

int main(void) {
  static unsigned char block[65536];
  unsigned char digest[32];
  unsigned int index;
  unsigned int iteration;
  for (index = 0u; index < sizeof(block); index++)
    block[index] = (unsigned char)(index * 29u + 17u);
  for (iteration = 0u; iteration < 3200u; iteration++)
    cupidbuild_host_sha256_bytes(block, sizeof(block), digest);
  for (index = 0u; index < sizeof(digest); index++)
    printf("%02x", digest[index]);
  puts("");
  return 0;
}
