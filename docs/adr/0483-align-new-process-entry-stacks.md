# Align new process entry stacks

- Status: Accepted
- Date: 2026-11-17

CupidC expects ESP modulo 16 to be 12 at an i386 function entry. The old
scheduler put an optional argument and a return address on the stack and
jumped to the entry function. The one-argument form entered at residue 8.
A process that returned then entered the exit routine through RET, which
also gave the routine the wrong stack alignment.

Prepare a cdecl frame for a trampoline inside the process stack. Its three
arguments carry the entry function, the optional argument word and whether
the entry takes that argument. The trampoline calls the entry function
with the appropriate signature and calls process_exit if it returns. The
context switch and the quiescent reaper keep their existing contracts.

The process test switches to the prepared stack and executes the new entry
path. Both forms return into process exit, detach from the CPU and release
their stack on reaping. The one-argument test fails on the old scheduler
before it enters the misaligned frame. This prepares the scheduler for a
compiler worker with its own stack and a request argument.
