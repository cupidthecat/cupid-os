# ADR 0476: Enable hosted heap serialization before worker start

The complete image-input owner needs concurrent Windows metadata checks to
meet its existing UNC capacity deadline. Its parent resolvers allocate memory;
the hosted allocator in ADR 0461 shares free lists and region metadata between
calls. Running those resolvers on workers requires heap serialization first.

The hosted runtime now offers an opt-in, process-lifetime heap mutex. The
original thread enables it before creating concurrent heap users. Public
`malloc`, `calloc`, `realloc` and `free` wrappers lock around the existing
allocator cores. Internal allocator calls use private unlocked entry points,
so realloc and calloc cannot acquire the same mutex recursively. Acquire and
release operations use CupidC's existing GNU atomic builtins.

This keeps ordinary command execution single threaded and preserves the
allocator's allocation, overflow, coalescing, release and failure policies.
It does not provide thread-local errno, synchronized streams or a general
threading library. Caller-owned worker lifetime and callback ordering remain
separate responsibilities. Installed seeds and normal build ownership remain
unchanged. See the [qualification record](../bootstrap/HOSTED-HEAP-THREADS.md).
