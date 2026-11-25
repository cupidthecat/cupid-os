"""Production SHA-256 helpers under arbitrary legal stream read partitions."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
LENGTHS = (0, 1, 3, 55, 56, 57, 63, 64, 65, 127, 128, 129,
           511, 512, 513, 65535, 65536, 65537, 131073)
CHUNKS = (1, 7, 31, 55, 56, 63, 64, 65, 127, 128, 65535)
DRIVER = r'''
int main(void) {
  static const size_t lengths[] = {0,1,3,55,56,57,63,64,65,127,128,129,
                                  511,512,513,65535,65536,65537,131073};
  static const size_t chunks[] = {1,7,31,55,56,63,64,65,127,128,65535};
  unsigned char *bytes = (unsigned char *)malloc(131073u);
  unsigned char digest[32];
  size_t index;
  size_t length_index;
  size_t chunk_index;
  if (bytes == NULL) return 1;
  for (index = 0u; index < 131073u; index++)
    bytes[index] = (unsigned char)(index * 37u + index / 7u);
  for (length_index = 0u; length_index < sizeof(lengths)/sizeof(lengths[0]); length_index++) {
    size_t size = lengths[length_index];
    cupidbuild_host_sha256_bytes(bytes, size, digest);
    printf("B %u ", (unsigned int)size);
    for (index = 0u; index < 32u; index++) printf("%02x", digest[index]);
    puts("");
    for (chunk_index = 0u; chunk_index < sizeof(chunks)/sizeof(chunks[0]); chunk_index++) {
      cupidbuild_sha_context_t context;
      size_t offset = 0u;
      size_t chunk = chunks[chunk_index];
      cupidbuild_sha_initialize(&context);
      while (offset < size) {
        size_t count = size - offset;
        if (count > chunk) count = chunk;
        cupidbuild_sha_update(&context, bytes + offset, count);
        cupidbuild_sha_update(&context, bytes, 0u);
        offset += count;
      }
      cupidbuild_sha_finish(&context, digest);
      printf("S %u %u ", (unsigned int)size, (unsigned int)chunk);
      for (index = 0u; index < 32u; index++) printf("%02x", digest[index]);
      puts("");
    }
  }
  free(bytes);
  return 0;
}
'''


def caller_source():
    # Compile the actual pure hash implementation, including its public wrapper.
    # Regular file reads do not reliably force short, non-block-aligned returns.
    source = (ROOT / 'toolchain/cupidbuild_host.cc').read_text(encoding='utf-8')
    first = 'typedef unsigned int cupidbuild_sha_word_t;'
    last = 'static int cupidbuild_host_copy_text('
    assert source.count(first) == 1 and source.count(last) == 1
    implementation = source[source.index(first):source.index(last)]
    return '#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n' + implementation + DRIVER


class CupidBuildStreamHashTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if path := os.environ.get('CUPIDBUILD_HASH_PRODUCTS'):
            cls.products = Path(path).resolve()
            cls.products.mkdir(parents=True, exist_ok=True)
        else:
            cls.temporary = tempfile.TemporaryDirectory(prefix='cupid-stream-hash-')
            cls.addClassCleanup(cls.temporary.cleanup)
            cls.products = Path(cls.temporary.name)
        caller = cls.products / 'hash-caller.cc'
        caller.write_text(caller_source(), encoding='ascii', newline='\n')
        if path := os.environ.get('CUPIDBUILD_HASH_PROGRAM'):
            cls.program = Path(path).resolve(strict=True)
        else:
            compiler = shutil.which('clang')
            if not compiler: raise RuntimeError('Clang is required for the native hash contract')
            cls.program = cls.products / ('hash-contract.exe' if os.name == 'nt' else 'hash-contract')
            command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-x', 'c', str(caller), '-o', str(cls.program)]
            result = subprocess.run(command, capture_output=True, timeout=180)
            (cls.products / 'compile.stdout').write_bytes(result.stdout)
            (cls.products / 'compile.stderr').write_bytes(result.stderr)
            if result.returncode: raise AssertionError(result.stderr.decode(errors='replace'))
        command = [str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=240)
        (cls.products / 'hash.stdout').write_text(result.stdout, encoding='ascii')
        (cls.products / 'hash.stderr').write_text(result.stderr, encoding='ascii')
        (cls.products / 'command.json').write_text(json.dumps({'command': command, 'exit_code': result.returncode}))
        if result.returncode: raise AssertionError(result.stderr)
        cls.rows = [line.split() for line in result.stdout.splitlines()]
        cls.bytes = bytes((index * 37 + index // 7) & 255 for index in range(max(LENGTHS)))
        if len(cls.rows) != len(LENGTHS) * (1 + len(CHUNKS)):
            raise AssertionError('hash contract returned unexpected row count')

    def test_public_byte_digests(self):
        rows = [row for row in self.rows if row[0] == 'B']
        self.assertEqual([int(row[1]) for row in rows], list(LENGTHS))
        for row in rows:
            with self.subTest(size=row[1]):
                self.assertEqual(row[2], hashlib.sha256(self.bytes[:int(row[1])]).hexdigest())

    def test_arbitrary_read_partitions_and_zero_length_updates(self):
        rows = [row for row in self.rows if row[0] == 'S']
        self.assertEqual([(int(row[1]), int(row[2])) for row in rows],
                         [(size, chunk) for size in LENGTHS for chunk in CHUNKS])
        for row in rows:
            with self.subTest(size=row[1], chunk=row[2]):
                self.assertEqual(row[3], hashlib.sha256(self.bytes[:int(row[1])]).hexdigest())


if __name__ == '__main__': unittest.main()
