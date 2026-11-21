# Clear arena bytes when allocated

CupidC repeatedly opens temporary arena blocks while compiling the frontend.
The core cleared the entire block as soon as it acquired it, including space
the job never used. The counted allocation entry point then cleared its own
range a second time.

Clear the requested range before returning each allocation. The counted entry
point keeps its overflow check and delegates the allocation. Rewind still
clears discarded bytes in a live block and checks the mark's generation.

Both the fresh-block and in-block paths must return zeroed bytes at the
requested alignment. The core contract exercises retained-block rewind and
recycled storage. A guest replay with this change persisted the same complete
CupidC frontend object as the checked host build.
