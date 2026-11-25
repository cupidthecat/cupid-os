# Stream HomeFS snapshots to FAT16

A HomeFS save used to allocate room for the whole serialized tree.
The 200 MiB release replay could link the kernel and stage its assets,
then fail while allocating the second copy for publication.

Keep the existing HFS1 records. Before writing, HomeFS records parent
indices and calculates the exact size. The FAT16 replacement
writer requests the header, node records, names, and file bytes through
a reader callback. It writes bounded sectors to a new chain, flushes
that chain, and publishes the directory entry before releasing the old
chain.

Keep the storage guard through the FAT16 write and any rollback. The
kernel can then finish the transaction before it terminates the owning
process. Other HomeFS opens and mutations wait for the guard. Reentrant
sync skips the in-flight snapshot because the caller cleared the dirty
flag before starting the write.

Pass reserved-file authority through the private FAT16 writer call.
Ordinary FAT writes still reject the HomeFS container during a save.
The caller can retry a failed snapshot with the prior container intact.
