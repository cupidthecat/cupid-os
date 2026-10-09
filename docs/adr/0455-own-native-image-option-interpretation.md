# ADR 0455: Own native image option interpretation

## Status

Implemented and tested privately on 2026-10-09. Source-plan integration,
discovery, observer lifetimes and the normal image command handoff remain open.

## Context

The native disk publisher can capture and publish an image, but its normal
caller still needs the existing image command's required values, repeated
options, stage order, WAD groups and geometry. The separate stage splitter and
WAD alias functions do not own a complete argument request or its storage.

## Decision

Give image argument interpretation an owner with a borrowed view. Borrow argv
path and explicit-stage strings; own the stage vector and generated WAD
destinations until close. Keep explicit stages before the final WAD group,
last-value behavior, unique long-option prefixes, both basename profiles,
full sector values and the existing 4,096-stage capture bound. Failure clears
the owner and view. The parser observes no filesystem state and grants no
release or source authority.

Retain the current hosted decimal digit/space profile in a C table, and compare
every digit family and accepted whitespace character independently with each
host's original parser. Keep filesystem resolution, FAT naming, checked seed
authority and publication with the command caller.

The normal snapshot includes every top-level Toolchain header. Hold the new
source outside that closure until the current 99-input consumer queue closes;
integrating the headers changes the subsequent producer snapshot.

## Evidence and limits

Four native/Cupid callers pass all 25 methods, for 100 executions and 780 actual
invocations. Independent rereading checks 113 source/support controls, complete
programs and output sequences, four whole checked object pairs, strict profiles,
qualified execution tools and all 99 committed normal inputs. Fifteen installed
root seeds remain unchanged.

The capacity fixture uses the represented full-width conversion after its first
checked build encounters an undeclared `strtoul`. Binary stdout repairs the
native Windows fixture's CRLF difference while preserving exact byte comparison.
Both original failures remain recorded. The options implementation does not
change across either fixture repair.

[The implementation record](../bootstrap/NATIVE-IMAGE-OPTIONS.md) retains exact
source, execution and failure scopes. Complete CLI compatibility, path
discovery, physical-root deduplication, observer/argv lifetimes, whole publisher
execution and accepted large geometries remain required before recipe adoption.
