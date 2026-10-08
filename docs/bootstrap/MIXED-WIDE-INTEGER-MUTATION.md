# Narrow stores after wide integer calculations

The retained compile caller estimates source-bundle capacity with `size_t total`
and a full-width file extent: `total += overhead + inputs[index].snapshot.size`.
The extent is checked against the bounded remaining memory capacity before this
calculation. CupidC accepts the corresponding ordinary calculation and
assignment conversion but rejects the compound assignment in Linear IR.

Permit a represented byte, halfword, word or integer bit-field destination to
use a 64-bit integer computation after promotion and the usual arithmetic
conversions. Reuse the existing binary operation and assignment conversion,
then store the destination's exact width. The address is evaluated once; the
existing lowering retains one semantic load and store. Shift computations still
use the promoted left type and require an independently promoted word count.
Atomic and Boolean mutation and partial volatile bit-fields retain their earlier
unsupported diagnostics. No new IR kind, emitter path or source cast is needed.

Both qualified compilers reject the new complete runtime fixture before the
change, preserving its previous output. The focused reproducer separately
shows ordinary assignment narrowing and word-only compound controls passing.
The fixture covers all ten compound operators, each represented narrow width,
signed and unsigned division and remainder by values above four GiB, enums,
mixed signedness, chained expression results, exact volatile stores, adjacent
bit-fields and one-time destination and operand evaluation. Useful negative
cases preserve output bytes and timestamps.

All four native and Cupid-built compiler callers pass the two new methods,
four frame-load methods and four wide-pointer methods. These 40 method
executions have no skips. Six wide, narrow and bit-field mutation IR/object
selections also pass on each native host. The old mixed-calculation rejection
cases now require successful lowering and object production; malformed-unit
and other unsupported cases remain.

| Compiler caller | Complete selection seconds |
| --- | ---: |
| Native Windows | 6.692 |
| Cupid-built Windows | 12.018 |
| Native Linux | 2.462 |
| Cupid-built Linux | 10.617 |

Independent rereading passes in 2.342 seconds. It checks every retained compiler
output, linked program, actual command, source control and complete compiler
build artifact. All four runtime objects agree byte for byte. The complete
selection retains 44 negative compiler calls, twelve runtime executions and
twelve native IR/object selections. Evidence under `C:/Users/admin/cp7/` is
`mixed-wide-four-producer-independent1-products.json`; the accepted controls
use `mixed-wide-native-windows3`, `mixed-wide-native-linux3`,
`mixed-wide-checked-windows1` and `mixed-wide-checked-linux1`.

The first native fixture build uses an automatic aggregate initializer with a
volatile field, which the existing compiler does not support. Ordinary member
initialization repairs that fixture without changing the mutation guard. The
next native object contract still expects the newly supported compound cases
to fail. Its corrected cases require success, preserve the frontend unit and
rewind the output before later negative checks. Both original failures and
source copies remain recorded.

The new Cupid-built compilers build the complete retained-seed caller and its
shared compile path without rewriting the active capacity expression. The
public compile regressions in [the seed-capture record](RETAINED-SEED-CAPTURE.md)
also pass through all four caller profiles. These are private capability and
consumer checks. The installed 99-input seed cohort remains separate from this
103-input prototype. Complete fixed-point qualification, paired adoption and
normal recipe ownership remain open. The compiler extension needs qualification
in the existing source cohort before that qualified parent can build and qualify
the broader publisher cohort.
