# Resume FAT16 allocation within a file

HomeFS saves large files by reserving a new FAT16 chain. Starting each
reservation at cluster 2 rereads sectors the same write has already scanned.

Start each file at cluster 2 and resume after the last reservation. If the
scan reaches the end, check the earlier entries once before reporting a full
volume. Stop immediately on a sector error. Advance the cursor only after
the FAT copies accept the end marker. The existing chain cleanup and final
sync still cover a failed allocation or write.

With 2,651 reservations, the allocator fixture reads 7,980 sectors. The
previous scan read 93,330. Both versions perform 5,302 writes and produce
the same FAT bytes. The fixture also covers wraparound, exhaustion, geometry
limits and a failed write before the scan can wrap.
