# Space periodic block-cache flushes

The timer driver runs registered callbacks on each PIT interrupt. The
block-cache callback was registered directly, so the system checked for
dirty sectors at 100 Hz. A guest kernel link encountered this during large
HomeFS saves, as the timer repeatedly wrote back the sectors used to build
the next FAT16 chain.

Register a kernel callback that measures five seconds of elapsed PIT ticks
before it asks the block cache to flush. Start the interval when the callback
is registered. Unsigned tick subtraction handles a counter wrap. Explicit
`sync` and file publication still flush at their own durability boundaries.

The callback fixture checks the first flush, the next interval, a different
PIT rate, counter wrap, and a zero rate. The old direct registration fails
the fixture.
