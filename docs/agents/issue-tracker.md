# Issue tracker: GitHub

Issues and PRDs for this repository live as GitHub issues in `cupidthecat/cupid-os`. Use the `gh` CLI for all operations. Run commands inside this checkout so `gh` selects the configured repository automatically, or pass `--repo cupidthecat/cupid-os` explicitly.

## Conventions

- Create an issue with `gh issue create --title "..." --body-file <path>`. Create multi-line body files with the current shell's native mechanism so the workflow works on both Windows and Linux.
- Read an issue with `gh issue view <number> --comments`, filtering comments by `jq` and also fetching labels.
- List issues with `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` and the appropriate `--label` and `--state` filters.
- Comment on an issue with `gh issue comment <number> --body "..."`.
- Apply or remove labels with `gh issue edit <number> --add-label "..."` or `--remove-label "..."`.
- Close an issue with `gh issue close <number> --comment "..."`.

## Pull requests as a triage surface

Do not treat pull requests as a request surface. The `/triage` workflow must not pull external pull requests into the issue triage queue. A purpose-built PR workflow may still inspect a pull request when explicitly requested.

GitHub shares one number space across issues and pull requests, so a bare `#42` may be either. Resolve ambiguity with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue in `cupidthecat/cupid-os`.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments` inside this checkout.

## Wayfinding operations

The `/wayfinder` skill uses one map issue with child issues as tickets.

- A map is a single issue labelled `wayfinder:map` whose body contains Notes, Decisions-so-far, and Fog. Create it with `gh issue create --label wayfinder:map --body-file <path>`.
- A child ticket is an issue linked to the map as a GitHub sub-issue (`gh api` on the sub-issues endpoint). Where sub-issues are unavailable, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels are `wayfinder:<type>` (`research`, `prototype`, `grilling`, or `task`). Once claimed, assign the ticket to the driving developer.
- For blocking relationships, use GitHub's native issue dependencies as the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/cupidthecat/cupid-os/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric database ID from `gh api repos/cupidthecat/cupid-os/issues/<n> --jq .id`, not the issue number or `node_id`. Where dependencies are unavailable, put `Blocked by: #<n>, #<n>` at the top of the child body. A ticket is unblocked when every blocker is closed.
- To query the frontier, list the map's open children, drop any with an open blocker or an assignee, and take the first remaining ticket in map order.
- Claim a ticket with `gh issue edit <n> --add-assignee @me`; this must be the session's first write.
- To resolve a ticket, comment with the answer, close the child issue, then append a context pointer and link to the map's Decisions-so-far.
