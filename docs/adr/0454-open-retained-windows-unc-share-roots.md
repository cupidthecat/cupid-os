# ADR 0454: Open retained Windows UNC share roots

## Status

Implemented and tested privately on 2026-10-08. Normal source integration,
complete producer qualification and native image discovery remain open.

## Context

The image command can select external Windows paths. The retained observer
previously resolves its root but accepts only a drive anchor, even though the
existing UTF-8 Windows adapter supports ordinary UNC paths. Native discovery
cannot retain a UNC payload's authority through that observer.

## Decision

After the existing absolute-path conversion, select either the ordinary drive
anchor or an ordinary UNC server/share anchor. Validate the server and share
components with the existing strict UTF-8/name rules. Open that anchor with the
same read-only directory access, sharing and reparse-point flags. Retain every
descendant through the unchanged component-by-component handle walk.

Device namespaces, incomplete shares, unavailable roots, regular-file roots and
reparse-point descendants remain rejected. The existing observer limits,
metadata/digest/absence checks, failure poisoning and cleanup remain unchanged.
The root identity comparison retains its ordinary revalidation and grants no
new source authority. No runtime import, public type or publication exemption
is added. POSIX source behavior is unchanged.

## Evidence

Native and new normal-source Cupid callers execute all 24 UNC methods on
Windows, for 48 method executions and 54 actual invocations. Real local shares
cover ordinary and long Unicode roots, share-only anchors, case/separator and
localhost/address aliases, drive/share physical identity, streamed payloads,
metadata drift, same-size edits with restored timestamps, missing-file
appearance and useful root-opening rejections. The ordinary four-caller
selection also passes 52 executions with four expected platform skips.

Independent checking rereads 193 source/support controls, actual programs,
compiler preparations, complete results, strict profiles and original bounds.
All Linux checked objects and the complete program match the preceding accepted
root-comparison products byte for byte. Both hosts' checked objects/programs
also match across the fixture-only repair. All 99 normal producer inputs and
fifteen installed seed files remain unchanged.

The first junction test resolves its path to the target before invocation.
Preserving its absolute spelling exposes the actual junction to the observer,
which rejects it. The first independent checker retains one old artifact
directory name; the correction preserves every acceptance predicate. Both
failed receipts and source copies remain retained.

[The implementation record](../bootstrap/OBSERVER-UNC-ROOTS.md) gives exact
source, execution and failure scopes. Full publisher execution with this new
host copy, absolute discovery/lifetime integration and normal recipe adoption
remain separate acceptance requirements. TempleOS remains read-only and excluded.
