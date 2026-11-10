# Immediate rotate-call prototype

After validating a literal count and a call to a proven local helper, the
emitter can replace their complete span with one immediate ROR or ROL.
It preserves value evaluation and abstract-stack effects. An incoming branch
to the call blocks selection. Other call forms and counts outside 1 through
31 keep ordinary emission. See
[ADR 0443](../adr/0443-fold-validated-immediate-rotate-call-spans.md).

Both hosts pass ten retained native frame and rotate-call controls. The
fixture checks immediate counts one, seven and thirty-one, dynamic counts,
reversed arguments, live values, external and indirect calls, `noinline`,
qualified helpers and argument side effects. Two failed matcher/assertion
attempts remain retained. Evidence uses `rotate-immediate-emitter1-native3-*`.

Current Cupid compilation and the complete large-publication suite are
running with the existing deadlines. A new complete compiler cohort still
requires behavior qualification, independent source and artifact checks,
reviewed code-shape expectations, capture and ownership updates, frontier
measurement and normal OS acceptance. The earlier frame prototype's 35
failed object controls remain open. Installed tools and recipe owners are
unchanged.
