# Domain docs

Cupid OS is a single-context repository. Engineering skills should read the domain documents listed below before exploring the codebase.

## Read before exploring

- Read `CONTEXT.md` at the repository root for the shared domain glossary and model.
- Read `docs/adr/` for architectural decisions that touch the area about to be changed.

If either location does not exist, proceed silently. Do not flag its absence or suggest creating it preemptively. The `/domain-modeling` skill, reached through `/grill-with-docs` and `/improve-codebase-architecture`, creates these documents lazily when terminology or decisions are resolved.

## File structure

```text
/
|-- CONTEXT.md
`-- docs/
    `-- adr/
```

Do not introduce `CONTEXT-MAP.md` or context-scoped `CONTEXT.md` files unless the repository is deliberately migrated to a multi-context model and this configuration is updated at the same time.

## Use the glossary's vocabulary

When an issue title, refactor proposal, hypothesis, or test name refers to a domain concept, use the term defined in `CONTEXT.md`. Do not drift to synonyms that the glossary explicitly avoids.

If the glossary lacks a needed concept, reconsider whether the language belongs to the project. If the missing term exposes a real gap, note it for `/domain-modeling`.

## Flag ADR conflicts

If proposed work contradicts an existing ADR, surface the conflict explicitly rather than silently overriding the decision. Name the ADR and explain why reopening it may be warranted.
