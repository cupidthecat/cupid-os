# Lower direct labels and goto through CupidC IR

## Context

The shared CupidC frontend already publishes canonical function-scope labels. A `LABEL` statement and every matching `GOTO` refer to the same entry in the translation unit's label table, so forward references are resolved before IR lowering begins.

Active toolchain source needs this control flow. `ld_serialize` in `toolchain/cupidld.c` uses `goto done` to reach one cleanup label after any setup or serialization failure:

```c
ctool_status_t status = ld_find_entry(link, &entry);
if (status != CTOOL_OK) {
  goto done;
}
```

The `done` label owns the common diagnostic and buffer cleanup path. Rewriting the function into duplicated cleanup blocks would hide a real compiler requirement and make the source harder to maintain.

Labels can also change reachability. A forward jump may enter a nested compound or one arm of an `if`. A backward jump may form a loop. A jump after an unconditional return is unreachable and must not revive its target.

## Decision

Before lowering a function, `ctool_c_lower_ir` validates its contiguous label slice. Every label record must name a `LABEL` statement in the same function, and every `LABEL` or `GOTO` statement must refer back to that slice. The existing source-order statement ownership check now covers label records as well.

Lowering runs a private fixed-point reachability pass over the supported statement tree. It begins at the function entry and marks a label only when a reachable `goto` names it. Later passes follow newly reachable labels, which handles forward chains and backward cycles without treating every syntactic target as live. Loop frames record reachable `break` and `continue` statements so the pass can tell whether an infinite loop reaches the following statement. A jump into a terminal `do` body does not create false fallthrough.

A label is a zero-width instruction target. A backward `goto` receives that function-relative target immediately. A forward `goto` emits the existing `JUMP` instruction with a private patch tag. When its label is lowered, the patch is resolved and cleared. Published IR contains only ordinary `JUMP` records with function-relative references.

An unreachable subtree is still checked against the supported-language boundary. If it contains a reachable label, lowering emits the structured subtree so the label has a stable target. Instructions before that label can remain in the serialized stream, but the incoming jump bypasses them. The function's final fallthrough decision comes from the reachability pass rather than from this dead structured prefix.

A dead structured prefix can leave its unused exit branch aimed at the current end of the function. When reachability proves that the function cannot fall through, lowering gives that branch a small unreachable return block. The incoming `goto` still lands on the label and cannot reach the block. This keeps every published branch target inside its function, including labels nested in a terminal `if` or `while`.

Count-only validation does not update live label targets. This keeps validation of unreachable statements from changing forward-jump state in either the count pass or the fill pass.

Declaration ownership follows a label's body. No public IR record, calling convention, or ELF relocation rule changes. Direct jumps stay inside one function, so the object emitter resolves them to machine-code displacements and emits no `.rel.text` entry.

At the time of this decision, hosted IR did not lower `switch`, `case`, or `default`. ADR 0038 adds that support and combines switch exits with the loop and label reachability rules. Computed `goto` and GNU label addresses remain outside this decision.

## Consequences and evidence

The direct contract covers forward and backward jumps, a two-label cycle, and an unreachable jump after a return. Four functions publish 39 exact IR instructions. A second contract covers entry into a nested compound, entry into an `if` arm, a jump after an ordinary loop exit, and entry into a terminal `do` body. It also jumps into an otherwise unreachable infinite loop before `break` and `continue`, then checks whether the function can fall through. A final function enters a label above a compound declaration. These seven functions originally published 46 instructions. ADR 0038's entry-aware lowering removes 12 dead prefix instructions, so the current proof publishes 34.

The deterministic object contract contains a 44-byte forward-jump function, a 76-byte backward loop, 38-byte terminal `if` and `while` functions, and a 41-byte function that enters a label above an automatic declaration. Its 237-byte `.text` section has six symbols including the null symbol and no relocations. The shared decoder checks branch targets at byte offsets 28 and 36 in the first function, 62 and 3 in the second, 22 and 30 in each terminal structured function, and 11 in the declaration function. The declaration uses one four-byte stack slot. Repeated emission is byte-identical and preserves the frozen input.

Negative contracts reject a `goto` with an expression payload, a jump to another function's label, a label record that names the wrong statement, and a missing label table. IR failure preserves the frozen input. Object failure also leaves output empty and rewinds allocations made during the operation.

Active-source guards pin the first `goto done` and the cleanup label in `toolchain/cupidld.c` with LF and CRLF spellings. The linker source itself is unchanged.

The first reachability attempt treated every syntactic target as reachable. That revived a label named only by an unreachable jump after a return, so the fixed-point pass replaced it. The first nested-label implementation also left an unused structured exit one instruction past a terminal function. The terminal `if` and `while` object tests caught that error before publication, and the unreachable return block now gives those exits valid targets. An early attempt to add the new fixtures to the large `active-leaf` contract exceeded that contract's existing job resource ceiling. Separate `forward-goto` and `nested-goto` modes keep the public seams focused and deterministic.

This is hosted bootstrap evidence. GCC or Clang still builds the shared modules and contracts, while the private in-kernel compiler produces every normal OS C object. The change transfers no build ownership and does not need an OS boot claim.

Issue #25 remains open. `switch`, broader values and addresses, additional local representations and storage durations, production integration, and staged self-hosting remain.
