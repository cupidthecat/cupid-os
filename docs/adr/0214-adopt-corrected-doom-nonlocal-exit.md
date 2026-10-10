# ADR 0214: Adopt corrected Doom nonlocal exit

## Status

Accepted on 2026-08-02.

## Context

CupidC and its checked seed can preserve live operands across a direct
`returns_twice` call. Active dglibc still used the older setjmp template,
which saved the stack pointer at the return-address slot rather than the
caller's post-return stack. Doom also assumes process teardown after
`I_Quit` or `I_Error`, while Cupid OS runs Doom repeatedly inside one shell
process. Exit callbacks and the recursive-error guard could therefore leak
from one launch into the next.

## Decision

Use the corrected 31-byte `dg_setjmp` body in active dglibc. It saves
`ESP + 4`, is declared `returns_twice`, and resumes with the caller's normal
post-return stack. `dg_longjmp`, `dg_exit`, and `dg_abort` are `noreturn`.
A zero jump value is still normalized to one.

Wrap each `doom_main` invocation in one live jump envelope. The landing path
clears the completed session's callback nodes and error guard only after the
nonlocal exit has left the callback walk. A defensive reset runs at session
entry. If the game loop ever returns normally, it uses `I_Quit` so registered
shutdown work still runs.

Keep Doom's callback semantics: registrations remain LIFO, normal-only
callbacks are skipped during `I_Error`, and duplicate registrations are not
collapsed. `D_Endoom` no longer exits from inside the walk. `I_Quit` and the
final `I_Error` path always leave through dglibc after callbacks finish.

## Evidence

Decoder-driven i386 tests still model the first and second return values and
check the exact corrected jump bodies. Checked-seed tests preserve live
expression operands, reject an unsafe reachable continuation, and reproduce
all three compatibility objects twice. The full exact Doom profile also
compiles with the checked seed.

The asset-free guest self-test uses the real `I_AtExit`, `I_Quit`, and
`I_Error` paths for two generations each. It checks LIFO order, normal/error
filtering, absence of reused callbacks, and reset of the recursive-error
guard. Direct `dg_longjmp` and `dg_exit` cycles prove that the active assembly
lands back in the shell envelope.

Separate four-vCPU runs on e1000 and RTL8139 each launch two different missing
IWAD paths in one shell session, require both failures to return, and then run
the expanded dglibc diagnostic. A second run on each NIC completes the full
stateful frontier after the swap feature has retained one FAT handle. Those
runs prove that the lifecycle envelope still works after earlier subsystems
have changed global state; they do not substitute for an IWAD-backed normal
quit.

## Rejected alternatives

Resetting or freeing callbacks from inside the dispatcher was rejected.
Callbacks may themselves enter quit or error handling, so mutation during the
walk would create use-after-free and skipped-callback hazards.

Leaving `D_Endoom` responsible for exit was rejected. Its early exit prevents
later callbacks from running and makes callback order part of control flow.

Keeping the old assembly until a staged WAD was available was rejected. The
asset-free lifecycle test exercises the active ABI and shell-session boundary
without reducing Doom or weakening the later gameplay gate.

## Consequences

Active dglibc now consumes the compiler capability promoted by ADRs 0212 and
0213. Repeated Doom launches can return to one shell process without retaining
the previous launch's callback list or recursive-error state.

This does not prove a complete game session. Two missing-IWAD launches per NIC
cover repeated error recovery, while a staged IWAD is still needed for
repeated player-driven quits, gameplay, input, game audio, and menu-driven
save/load.
