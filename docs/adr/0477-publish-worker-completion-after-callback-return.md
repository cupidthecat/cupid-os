# ADR 0477: Publish worker completion after callback return

The source-owner worker prototype must retain caller-owned storage even when
a Windows wait fails. A callback's own final statement is an awkward lifetime
boundary: the callback has yet to return, and an adapter may still need its
descriptor. The adapter now publishes completion after callback return and
after its final descriptor read. Acquiring that marker permits release of the
descriptor and callback context. It does not mean the OS thread has terminated.

The private Windows descriptor contains a cdecl callback, argument and
completion-flag pointer. The i386 adapter checks all three required pointers,
preserves the callback result and publishes through XCHG before its stack-only
epilogue. The native adapter follows the same ordering. Pool callbacks no
longer publish their own completion. Successful waits remain the ordinary path;
a failed wait poisons validation while retaining storage through completion.

The callback fixture and both real pool seams have positive and failure
controls. The complete image caller still needs full regression and publication
acceptance before adoption. See the [qualification record](../bootstrap/WORKER-COMPLETION.md).
