# Lower C switch statements through CupidC IR

## Context

The shared CupidC frontend already publishes typed `SWITCH`, `CASE`, and `DEFAULT` statements. A switch condition has undergone integer promotion. Each case owns a folded integer constant converted to that promoted type, and the frontend rejects duplicate case values and duplicate defaults.

Active compiler source needs this control flow. `cfront_public_storage` in `toolchain/cupidc_frontend.c` maps six private storage classes to the public enum with a compact switch. Rewriting that function as a chain of conditionals would hide a real compiler requirement and make the source less clear.

Switches also interact with existing control flow. `break` selects the nearest loop or switch, while `continue` selects the nearest loop even when a switch lies between the statement and that loop. Case labels may sit inside an `if` or loop, nested switches own separate label sets, and direct `goto` may enter a case body through an ordinary identifier label.

## Decision

`ctool_c_lower_ir` evaluates a represented 32-bit switch condition once. A new `DUPLICATE_VALUE` instruction keeps the original condition on the abstract stack while a copy is compared with one case constant. Each comparison emits an equality result, a `BRANCH_ZERO` to the next comparison, and a private jump to the case statement. A matching path discards the saved condition before it jumps. The final path also discards the condition, then jumps to the default label or the switch exit.

Case and default targets remain zero-width statement positions. Their private patch tags are resolved while the switch body is lowered, and no tag survives in published IR. The first implementation uses a source-ordered comparison chain. It does not claim jump-table selection or optimization.

Loop frames become control frames tagged as loops or switches. `break` uses the top frame. `continue` searches outward for the nearest loop frame. This preserves all existing loop targets and gives a switch its own exit without treating it as a continuation target.

Dispatch discovery walks compounds, both `if` arms, loop bodies, and ordinary identifier labels. It stops at a nested switch, so inner cases cannot enter the outer dispatch chain. The fixed-point reachability pass also records switch entry separately from body fallthrough. Entering a switch makes each of its own case labels reachable, but a case from an unreachable nested switch does not revive that inner statement. The fixed-point result now owns the final fallthrough decision for every lowered function, not only functions that contain `goto`.

Statement lowering carries the same entry fact into nested compounds, selections, and loops. A switch body starts without ordinary source entry because control arrives through a case, default, or identifier label. Prefix statements remain validation-only until one of those labels becomes reachable. When an earlier `goto` enters an identifier label inside a switch, the condition is still validated, but the unreachable case-dispatch chain is not published. This keeps dead prefix jumps from leaving private patches in the result.

An ordinary identifier label does not make its body reachable on its own. Its body starts only through normal fallthrough or a `goto` that the fixed-point pass marked reachable. A nested case may still enter a later statement in that body without making the unused label's prefix live. Loop fallthrough uses the same entry fact, so a loop that is reached only through a nested case does not create a false source-entry path.

Enumerator identifiers lower to ordinary integer instructions when their public binding has no storage or linkage, carries a canonical 32-bit target value, and names a represented 32-bit integer type. A valid wider enumerator receives the existing unsupported-type diagnostic. A malformed noncanonical value receives the invalid-unit diagnostic.

Represented integer constants now accept both zero-extended unsigned values and canonical sign-extended signed values. IR retains the low 32 target bits, so a signed `case -1` compares against `0xffffffff` without admitting a wider value.

The i386 emitter implements `DUPLICATE_VALUE` by moving the top 32-bit value through EAX and pushing it twice. Switch jumps remain function-relative and produce no relocation.

This decision covers hosted IR and hosted ELF32 object emission only. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC emitter remains part of the embedded runtime JIT and AOT path. It still has its separate limitation for `continue` inside a switch nested in a loop. Narrow, wide, floating, pointer, and aggregate switch values remain outside the hosted slice, as do computed `goto`, GNU label addresses, production integration, and self-hosting.

## Consequences and evidence

The unchanged `cfront_public_storage` function lowers to 59 exact IR instructions with a maximum abstract stack depth of three. The condition is loaded and promoted once. Six dispatch blocks each contain one duplicate, constant, equality comparison, conditional branch, discard, and case jump. The final default jump shares the `CFRONT_STORAGE_NONE` target with the last case. Six enum returns keep their exact assignment conversions.

Its deterministic local ELF32 function is 272 bytes. The object has two symbols including the null symbol, no relocations, six decoded comparisons, six conditional branches, seven direct jumps, and six returns. The contract checks every byte, confirms every branch target stays inside the function, emits the object twice, and verifies that the frozen frontend unit and job arena are unchanged.

Control fixtures cover ordinary fallthrough, a switch without a default, a signed negative case, switch `break`, loop `break`, and `continue` through a nested switch to the enclosing `for` iteration. Each control jump must land on the exact source line for its switch exit, loop exit, or loop iteration. Nesting fixtures cover an inner switch, a case inside an `if`, cases inside `while`, `do`, and `for` bodies, direct `goto` into a case body, an unreachable inner switch, dead `break` and `goto` prefixes, and a reachable case below an unused identifier label. The unreachable inner switch publishes no comparison or return instructions from its private cases. A `goto` that enters a case body publishes only the entered path, not an unreachable dispatch or default path. The unused-label fixture publishes the case and default returns without publishing its dead prefix jump or dead target.

Negative contracts reject a represented 64-bit switch condition, an out-of-range case expression index, extra case or default payloads, and a noncanonical 32-bit enumerator value. Object failure for a malformed case leaves output empty and rewinds temporary allocations. Every IR failure preserves the supplied translation unit.

The first switch fixture reached the public unsupported-statement diagnostic. After dispatch lowering was added, enum return values reached the unsupported-expression diagnostic, which led to the general enumerator-identifier rule above. The first control fixture then exposed the old loop-only frame model because `break` inside a switch was rejected as invalid. The first nesting fixture showed two comparisons and four returns in an unreachable inner switch. Restricting case-label reachability to the nearest switch removed those dead inner paths.

Independent review found that signed negative case constants still failed at the old zero-extension boundary, a dead prefix `goto` could leave an unresolved private patch, and the control test counted jumps without checking their destinations. The added fixtures failed with the unsupported-type and internal diagnostics before the fixes. Canonical signed bits, entry-aware statement lowering, and exact target assertions made them pass. Follow-up review found that an unused identifier label still forced its body entry to reachable when a later case was the real entry. The new fixture failed with the internal unresolved-patch diagnostic before label lowering began using normal entry or reachable `goto` facts. Review also corrected local `while` and `for` fallthrough to use the same entry fact, added positive cases inside all three loop forms, and narrowed the general branch-range oracle to each function's own instruction slice. Two copies of the tagged-jump patch scan and a stale loop-only helper name were also removed. One patch helper and the `cir_lower_control_jump` name now describe the shared mechanism.

This change transfers no build ownership and retires no host dependency. It changes no kernel object, disk image, runtime path, or production ABI, so it does not need an OS boot claim.

Issue #25 remains open. Broader values and addresses, additional local representations and storage durations, production integration, staged self-hosting, and the final fixed-point bootstrap still remain.
