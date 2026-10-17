# ADR 0329: Retry transient native-tool cleanup locks

## Status

Accepted on 2026-08-24.

## Context

The checked-seed runner copies each native executable into a private temporary
directory before launch. On Windows, the child process can exit before the
operating system releases its executable image. Python then reports sharing
violation 32 while removing the private copy. A full build can fail after the
tool completed successfully, even when the seed and output are unchanged.

This happened in both parallel and serial production builds. Retrying the
build merely moved the failure to a different checked tool invocation.

## Decision

Native private-tool cleanup retries Windows sharing violation 32 up to 40
times, waiting 50 milliseconds between attempts. Cleanup still fails
immediately for every other filesystem error and still fails after a lock
persists for two seconds.

The retry applies only after a native private image has run. It does not
change seed capture, manifest validation, argument forwarding, timeouts, tool
status, output publication, or the post-run five-tool cohort check.

## Evidence

The focused cleanup contract injects two transient sharing violations and
requires the third removal to succeed. It also proves that an unrelated
filesystem error is not retried and that a persistent sharing violation stops
after the bounded attempt count. A timeout keeps its original exception if
cleanup also fails, while a launch failure uses ordinary cleanup without the
post-run retry.

The checked-seed module and normal production build provide the integration
gates. Their final results are recorded in the bootstrap log.

## Consequences

Windows builds no longer fail because a completed private executable remains
mapped briefly. A genuinely stuck image still blocks the build with the
original operating-system error.

No tool or artifact changes owner. Python remains the checked runner, WSL is
still required for Linux-seed execution on Windows, and no source suffix
changes. `TempleOS/` remains untouched reference material.
