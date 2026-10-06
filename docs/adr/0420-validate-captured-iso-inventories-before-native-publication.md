# ADR 0420: Validate captured ISO inventories before native publication

Date: 2026-10-05

Status: Inventory source capability; publication integration pending

CupidObj already authors the complete deterministic ISO fixture. Moving its
remaining Python coordinator into CupidBuild requires a captured fixture
inventory that the native independent checker can validate without depending
on the producer's internal node table or hosted filesystem paths.

Use the existing typed CupidObj ISO request and source views at this interface.
A separate, allocation-free native module validates the manifest's one-to-one
membership, exact case, source kinds, represented parents and current portable
path/depth limits. It accepts the entire 512-entry producer boundary. The
caller retains capture and filesystem observations; this module never acquires
or publishes files. A small report contains inventory counts and a 64-bit size
sum. Full image validation remains a separate responsibility.

Sharing the immutable request layout avoids a second set of logical-to-source
mappings while keeping the independent checker separate from CupidObj's
implementation. Calling the producer again would not supply an independent
check. Giving this module host paths would mix deterministic inventory policy
with platform observation and transaction lifetime.

Fourteen methods pass on each host, including an active-fixture comparison with
both the checked author and Python renderer, limits, rejection, restoration and
concurrency. Both checked CupidC images compile the module and contract. Their
four object/executable pairs match, and the linked Linux ELF contract runs with
status zero on native Linux and through WSL. No seed or normal recipe changes.
The handoff document records remaining native capture, full image checking,
transaction, staged qualification and guest feature-17 gates.
