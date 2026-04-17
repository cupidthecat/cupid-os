#!/bin/sh
# Generate a deterministic 4 KB file of pattern bytes for ISO9660 smoke.
# Pattern: repeating 0x00..0xFF so sector-crossing reads are easily verified.
set -e
out="$(dirname "$0")/big.bin"
python3 -c "
import sys
buf = bytes(i & 0xFF for i in range(4096))
with open(sys.argv[1], 'wb') as f: f.write(buf)
" "$out"
echo "Generated $out (4096 bytes, pattern 0x00..0xFF)"
