# ADR 0475: Update private derived integer lvalues

Status: Implemented and qualified on both hosts and in the installed guest.

Extend the private compiler's shared derived-update path to integer pointer
targets, indexed elements and record fields. An integer load retains its evaluated
address in EDX and its normalized value in EAX. The update stores once at the
declared byte, halfword or word width. Postfix preserves the old value; prefix
normalizes the stored result. Floating and SIMD updates retain their existing
EAX-address/XMM0-value convention.

Pointer array slots retain their declared pointer type so they cannot enter the
integer path with a byte-sized update. Statement member updates restore const
qualification from the owning record after resolving the member chain. This
prevents an earlier condition from supplying unrelated qualification.

Const objects, computed rvalues, incomplete rows and unsupported pointer-slot
updates retain useful diagnostics. Signed overflow has no additional guarantee.
The hosted producer and installed seed identities are unchanged.

[The capability record](../bootstrap/PRIVATE-INTEGER-UPDATES.md) tracks tests,
review failures, build qualification and remaining work.
