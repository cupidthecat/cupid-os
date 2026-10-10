# ADR 0414: Qualify native ABI behavior in both stage matrices

Both complete bootstrap matrices require the final two CupidBuild generations
to pass the native syscall ABI command. The gate captures the six current OS
declarations separately from the compiler producer snapshot. These declarations
are runtime test inputs, so adding them to the compiler build plan would blur
the producer's source boundary without strengthening compilation evidence.
The retained behavior record binds their sizes and hashes to the complete
independent oracle report and the observed command results.

The gate checks success, semantic and input failures, usage rejection and
recovery. It compares both generations, rejects malformed or partial reports,
and checks that commands preserve fixture membership, bytes and metadata.
Original declaration captures and staged executable bytes are rechecked around
each pair. These sequential observations detect drift without claiming an
atomic filesystem snapshot.

The command takes no seed manifest or release option. ADR 0411 continues to
authorize seeded operations during qualification; its runner leaves this
read-only command unchanged. A prepared stage bundle remains unqualified until
both host matrices pass. Installing a seed pair and transferring the normal
ABI recipe remain later release actions.
