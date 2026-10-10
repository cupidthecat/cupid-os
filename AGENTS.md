## Agent skills

### Cupid Toolchain bootstrap

The active engineering mission is to make Cupid OS self-hosting without reducing the operating system to fit its current tools. Before changing the compiler, assembler, disassembler, object/link path, build graph, or active C/assembly source, read `CONTEXT.md`, the relevant records in `docs/adr/`, and `docs/bootstrap/README.md`.

- Work incrementally on `bootstrap/cupid-self-hosting`; keep each coherent commit green.
- Extend CupidC and CupidASM for real active-source requirements. Do not remove behavior, prune vendored code, or rewrite source awkwardly to evade a toolchain limitation.
- Add positive and useful negative tests for every new toolchain capability. Run the relevant OS build and boot/runtime smoke when output or ABI behavior changes.
- Update `docs/bootstrap/` in the same commit with implementation decisions, limitations, failed approaches, test evidence, and ownership progress.
- Treat `TempleOS/` as read-only reference material. Never build it into Cupid OS or include it in progress metrics unless the user explicitly changes that scope.
- Stage files explicitly. Existing build artifacts and unrelated worktree changes belong to the user.

### Issue tracker

Issues are tracked in GitHub at `cupidthecat/cupid-os`; external pull requests are not a triage surface. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage roles use the canonical same-name label vocabulary. See `docs/agents/triage-labels.md`.

### Domain docs

This is a single-context repository with `CONTEXT.md` at the root and architectural decisions in `docs/adr/`. See `docs/agents/domain.md`.
