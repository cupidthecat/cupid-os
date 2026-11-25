# Preserve safe immediate pointer qualification conversions

- Status: Accepted
- Date: 2026-07-23

## Context

The unchanged CupidLD command stores its native paths as `char **` and passes them to a helper whose parameter is `char *const *`. This is a valid C qualification conversion. The helper may read each pointer but cannot replace one through that parameter.

CupidC rejected the call because its pointer relation did not combine qualifiers carried by a wrapper with qualifiers stored on the immediate referent node. After the frontend accepted the conversion, Linear IR rejected the published qualification conversion for the same reason.

Changing the helper signature or adding a cast would hide a compiler defect in active source. Accepting every nested qualifier difference would be unsafe. In particular, converting `char **` to `const char **` can let a pointer to const data be stored where the caller expects a pointer to writable data.

## Decision

The frontend now normalizes qualifiers from both representations of each immediate pointer referent before it classifies a pointer pair. It compares the unqualified immediate referent bases, permits qualification to be added at that level, forbids qualification removal, and keeps atomic qualification exact.

This rule accepts `char **` to `char *const *`. It does not treat a deeper `char *` to `const char *` difference as an immediate qualification change.

Linear IR applies the same normalization when it validates a frontend qualification conversion. The conversion keeps the destination pointer type but emits no machine-level value change because both types use the same four-byte representation.

## Rejected alternatives

Changing `cupidld_common_native_root` to take `char **` was rejected because the existing `const` qualifier states a useful write restriction.

Adding an explicit cast at the call was rejected because it would make correct source compensate for an incomplete type relation.

Ignoring qualifiers throughout the complete referenced type graph was rejected because it would admit unsafe deep qualification and qualifier removal.

Treating the conversion as a general pointer cast was rejected because the frontend already has a precise qualification-conversion kind and the representations are identical.

## Consequences and evidence

Focused frontend coverage accepts the valid call and checks that its argument is a destination-typed qualification conversion. Focused Linear IR coverage keeps it as a no-op conversion.

Negative coverage still rejects `char **` to `const char **` and `char *const *` to `char **`. Atomic qualifier checks and nested referent compatibility remain in force.

All 45 frontend contract modes and all 40 Linear IR contract modes pass. The unchanged `cupidld_main.c` now reaches deterministic i386 object emission and links into the Cupid-built CupidLD command described by ADR 0086.

This decision covers immediate pointer qualification only. It does not broaden casts, atomic conversions, or incompatible nested pointer types.
