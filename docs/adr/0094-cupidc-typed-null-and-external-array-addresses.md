# Lower typed nulls and external unspecified-bound arrays in CupidC

- Status: Accepted
- Date: 2026-07-23

## Context

The first kernel crypto cutover left `asn1.c`, `x509.c`, and `x509_chain.c`
with `CTD000006` conversion failures. The first two failures came from the
kernel's ordinary null macro:

```c
#define NULL ((void *)0)
```

The frontend correctly represented a local pointer initializer as an integer
zero converted to `void *`, followed by a destination-typed null conversion.
Linear IR accepted the first conversion but required the second conversion's
source to remain an integer. That rejected valid C after the frontend had
already retained its type.

Once that path worked, `x509_chain.c` reached a different boundary:

```c
extern const ca_root_t TLS_CA_BUNDLE[];
```

The declaration intentionally omits the array bound. Its element type is
complete, and C permits the array designator to decay to an element pointer.
The array itself remains incomplete. Linear IR had treated every file address
as either a scalar or a complete aggregate, so it rejected this legal address
before decay.

Changing the null macro, adding a made-up array bound, or spelling the source
around either limitation would have hidden a compiler gap in active code.

## Decision

A destination-typed null conversion may consume either a represented integer
or an i386 object pointer to unqualified `void`. The pointer source must not
have atomic qualification.

The frontend now records a null-constant semantic flag on the conversion node.
It sets the flag only after proving an integer constant expression has value
zero, either directly or through an explicit cast to `void *`. Asking the
frontend to publish a null conversion without that proof is an internal error.
IR validates the flag on every frozen expression: a null conversion must carry
it, and no other expression may do so. When the source is a typed `void *`, the
source node must also be the explicit cast retained by the frontend. A runtime
`void *` cannot enter this path by changing its conversion kind.

The conversion is representation-preserving, so the emitter keeps the
existing four-byte value and emits no conversion bytes. Object emission first
passes through IR validation, then applies the same source and destination
type checks to the lowered instruction.

An external array with an unspecified bound may produce a file address and
decay to an element pointer when all of these facts hold:

- The array layout is an incomplete object with size zero.
- The element is a complete object with nonzero size.
- Neither the array nor the element has atomic qualification.
- The target is the compatible pointer to that element type.

This rule does not make the array complete. Loading it as a value, allocating
storage from it, or accepting a variable-length or otherwise incomplete
aggregate remains unsupported.

The public contracts use the same shapes as the active sources. The null
fixture spells `NULL` as `((void *)0)`. The external-array fixture indexes
`TLS_CA_BUNDLE` and loads its `der` member from a 12-byte `ca_root_t`.

## Rejected alternatives

Rewriting the kernel null macro as an integer zero was rejected. The existing
macro is valid C and appears throughout active code.

Giving `TLS_CA_BUNDLE` a fixed bound at its declaration was rejected. The
consumer does not own that bound, and duplicating it would weaken the source
interface.

Treating every incomplete aggregate as addressable was rejected. The narrow
rule is justified by C's external array designator semantics and still checks
that the element has a usable target layout.

Moving these sources into the production CupidC cohort before refreshing the
checked seed was rejected. The normal build must use a compiler image that
contains the capability it claims to own.

## Consequences and evidence

The IR tests now prove direct integer zero, `((void *)0)`, and the computed
constant expression `(void *)(1 - 1)`. Transactional negatives cover missing
or misplaced provenance, a runtime `void *` relabeled with and without a
forged flag, and a non-`void` pointer source. Repeated lowering after those
failures produces the same instruction fingerprint. The tests also prove the
exact file-address, array-decay, scaled-index, member-address, load, and return
sequence for the external array. A mutated non-fixed incomplete array fails
transactionally with `CTD000003`, and a repeated lowering produces the same
instruction fingerprint.

The object tests emit each fixture twice without changing the frozen
translation unit. The two null functions, one using `((void *)0)` and one
using integer zero, have identical relocation-free machine code. The external
array object contains one undefined `TLS_CA_BUNDLE` symbol and one
`R_386_32` relocation. Its text scales the index by 12 and adds four for the
`der` member before loading the pointer.

The current compiler source compiles the three unchanged crypto files twice
under `KERNEL_I386`. Each pair is byte-identical and passes the production
i386 relocatable-object validator:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/crypto/asn1.c` | 9,032 | `51c5300859a80579bf27ef2f978bccfb53fdf02947c219caf01ad9bf826d872b` |
| `kernel/crypto/x509.c` | 16,072 | `82536ae0f6c1a026757f45236fe6ce2454b147764e12105e8dc0fe199bb09801` |
| `kernel/crypto/x509_chain.c` | 7,028 | `bbcad318a413be4cfce52541a0b803368ec1515e9ff4faa119552715bf0aec98` |

The checked seed predates this change, so production ownership stays at 16
crypto sources for now. These three files remain host-built until a refreshed
seed passes the staged fixed point. `csprng.c` is the remaining crypto source
with a language blocker because it uses GNU extended inline assembly.

The existing checked seed can compile the updated Toolchain source. A complete
staged build produces byte-identical stage-two and stage-three copies of all
19 C objects, startup, and five tools. The rebuilt CupidC is 1,829,508 bytes
with SHA-256
`ebbd9d3d1d303f9c7ed976aaead1460413e5d26b81cae501262d869e8be04c7a`.
The 40-input source snapshot is
`58ed0099722a6af81157d4531dc9f0af38b996bbf10f1b11e951df40a86347e4`.
This proves self-buildability but does not promote that image into the checked
seed in this decision.
