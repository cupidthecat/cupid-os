# Opt-in Swap in CupidOS

CupidOS has a handle-based swap system for callers that want more
logical memory than physical RAM holds. Swap is explicit — standard
`kmalloc` stays untouched.

## Quick start

```c
#include "swap.h"

/* One-time init, e.g. from the shell or a program: */
swap_init("/disk/swap.bin", 4 * 1024 * 1024);  /* 4 MB resident pool */

swap_handle_t h = swap_kmalloc(60 * 1024);  /* class 3 (64K) */
void *p = swap_pin(h);
/* ... read/write up to class_size bytes of p ... */
swap_unpin(h);

/* Later, maybe after other pins evicted us: */
p = swap_pin(h);   /* loaded from disk if needed */
/* ... */
swap_unpin(h);
swap_free(h);
```

## API

| Function | Returns | Notes |
|---|---|---|
| `swap_init(path, pool_bytes)` | 0 / -errno | Create/open 16 MB backing file. Pool split evenly across 4 size classes. Call once. |
| `swap_kmalloc(size)` | `swap_handle_t` or `SWAP_INVALID` | Rounded up to smallest class: 1K / 4K / 16K / 64K. Allocates disk slot; no RAM yet. |
| `swap_pin(h)` | `void *` or `NULL` | Loads handle into RAM. May evict LRU-unpinned handles of the same class to make room. NULL if class is fully pinned. |
| `swap_unpin(h)` | — | Decrement pin count. When count hits 0, handle becomes LRU-eligible for eviction. |
| `swap_free(h)` | — | Release disk + RAM. Panics if currently pinned. Invalid handles are silent no-op. |
| `swap_stats(&out)` | — | Dump per-class usage + eviction counter. |

## Size classes

- Class 0: **1 KB** — 4096 disk slots (4 MB region).
- Class 1: **4 KB** — 1024 disk slots.
- Class 2: **16 KB** — 256 disk slots.
- Class 3: **64 KB** — 64 disk slots.

## Shell commands

- `swapinit [pool_kb]` — default 4096 KB. Uses `/disk/swap.bin`.
- `swapstats` — dump current stats.

## CupidC bindings

All 5 core functions are bound for CupidC `.cc` programs:
`swap_init`, `swap_kmalloc`, `swap_pin`, `swap_unpin`, `swap_free`.

Example CupidC usage:
```c
void main() {
    swap_init("/disk/swap.bin", 1024 * 1024);
    int h = swap_kmalloc(8000);
    char *p = (char *)swap_pin(h);
    p[0] = 'X';
    swap_unpin(h);
    swap_free(h);
}
```

## Limitations

- **Opt-in only.** Standard `kmalloc` does not swap. Callers must be
  swap-aware.
- **16 MB hard cap** total swap file size.
- **1024 handles max.** Too many small allocations exhaust the handle
  table before disk fills.
- **64 KB max per allocation.** Class 3's 64 KB limit is the per-alloc
  ceiling; bigger buffers need to be split or use `kmalloc`.
- **No auto-cleanup.** Handles must be freed by the owner before
  process exit; otherwise disk slot leaks until reboot.
- **No persistence.** Swap file contents are scratch; don't rely on
  surviving reboot.
- **Single-threaded.** No locks (CupidOS kernel is single-threaded in
  this subsystem).

## Why opt-in?

Classic page-fault swap is architecturally hostile on a ring-0
identity-mapped kernel — every C dereference would need to be
page-fault-safe, including IRQ handlers. Opt-in handles deliver the
same user-facing capability (logical > physical memory) while leaving
the rest of the kernel untouched.

## Testing

- `bin/feature18_swap.cc` — end-to-end smoke test covering per-class
  round-trip, disk-cap exhaustion, nested-pin refcount, invalid-handle
  benign ops. Run after init:
  ```
  > swapinit
  > feature18_swap
  ```
  Expected output: `PASS feature18_swap`.
