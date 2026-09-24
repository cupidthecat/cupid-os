# ADR 0230: Carry object pointers in private word parameters

## Status

Accepted on 2026-08-04.

## Context

The active `/bin/ctxt.cc` source declares `ctxt_parse_action` with integer
address parameters, then passes `&action_type` and a character buffer at its
call site. Inside the function, those words are converted back to `int *` and
`char *`. This is a deliberate Cupid i386 address convention, and the private
compiler already represents each source value as one four-byte word.

Private CupidC nevertheless rejected the call while checking fixed cdecl
parameters. Rewriting the source declaration would change an active interface
to avoid a compiler limitation.

## Decision

Allow a represented object pointer argument to fill a fixed `int` or
`unsigned int` cdecl parameter in private JIT and AOT compilation. The
conversion preserves the one i386 word without arithmetic or truncation.

Keep the rule at the fixed-call coercion boundary. It does not permit an
object pointer in a `char`, floating, or SIMD parameter, and it does not add a
general pointer-to-integer expression conversion. Existing represented
pointer-category compatibility is unchanged.

## Evidence

A runtime contract passes `int *` and `char *` values through fixed signed
integer parameters, converts them back inside the callee, and verifies both
writes. A separate runtime contract carries an `int *` through an `unsigned
int` parameter and verifies the recovered address. Negative contracts reject
pointer arguments for narrow and floating parameters, preserve the useful
cdecl diagnostic, and compile and execute a valid call in the same compiler
process afterward.

The private call ABI module passes all of its cases. A source-driven census
clears the former fixed-parameter diagnostic at the unchanged
`/bin/ctxt.cc` call. That file is an include fragment rather than a runnable
program, so a direct JIT parse correctly reports that it has no entry point.
`/bin/notepad.cc` includes the complete fragment, supplies the entry point,
and passes private AOT compilation. An earlier serial census mistook the
include-only result for a timeout because its AOT diagnostic was visible only
in the GUI.

The final fresh-image census compiles 103 of 104 runnable programs. Browser
passes with 471,885 code bytes and 5,986,752 data bytes. Notepad passes with
114,607 code bytes and 501,807 data bytes. The one remaining failure is
`/bin/gfxgui_test.cc`, which reports 49 unresolved references across 46
distinct native names. Those missing GUI bindings are a separate compiler
frontier, not a cdecl conversion failure. The complete private call ABI module
passes all 124 tests in 20.717 seconds.

ADR 0233 later completes that separate binding frontier. This paragraph keeps
the measured 103-of-104 checkpoint that identified it.

## Rejected alternatives

Changing `ctxt_parse_action` to pointer parameters was rejected because the
active source should drive the compiler boundary. Applying ordinary integer
or floating conversion was rejected because this convention is a bit-exact
one-word transport, not numeric conversion.

Extending the numeric rule to narrow or floating destinations was rejected
because those slots would not preserve the represented address. Pointer
category compatibility remains the separate pre-existing rule.

## Consequences

Active Cupid source can use an object address in a fixed signed or unsigned
word parameter without weakening cdecl type checking elsewhere. The change
affects only the private in-kernel compiler and does not move a normal build
owner or change the checked hosted seed.
