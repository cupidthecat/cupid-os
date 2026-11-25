# Sync FAT data before publishing the directory

Large HomeFS saves write a new FAT16 cluster chain. The driver used to
sync the cache after every cluster and again after writing the last one.
The per-cluster sync was added for diagnostics.

Let the block cache handle writeback during the copy. Check its final sync
before publishing the directory sector. A data write or sync failure still
releases the unpublished chain. The directory is still flushed before the
previous chain is released.
