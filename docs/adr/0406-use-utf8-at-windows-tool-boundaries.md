# ADR 0406: Use UTF-8 at Windows tool boundaries

Date: 2026-09-24

Status: Implemented; canonical LF paired staged, publication and OS/runtime acceptance passed

Windows command-line and filesystem APIs previously interpreted narrow strings
through the active code page. That cannot represent every supported build path.
The six hosted tools now use UTF-8 internally and convert to UTF-16 at Windows
API boundaries. Native and Cupid-built tools share the same strict scalar codec.
Malformed input fails instead of replacing characters or trying another encoding.

## Entry and filesystem boundaries

Native Windows builds use a wide entry point and separate adapter objects.
Checked builds select explicit wide startup files, one adapter role per tool,
and exact role-specific imports. Ordinary, publication and CupidBuild roles
have different API requirements. Process launch, file operations, environment
lookup, directory enumeration and retained NT paths use the same encoding rule.
Linux keeps its existing entry and host objects.

Changing only argv would leave file discovery and child processes dependent on
the code page. Keeping conversions at each OS boundary also avoids changing
the compiler, assembler or portable host interface to UTF-16.

## Bootstrap generations

Historical ANSI seed profiles remain separately validated. The new candidate
captures 73 source files and binds the existing Linux plan to Windows plan
`a31575236059b77a47bb58c79072754258c4762d30105319c451e407b7353f99`.
The Windows plan has 30 C objects, three startup objects and six tools.
Publication captures 87 files. Manifest readers, artifact policy, Make inputs
and the pinned publication reader require the corresponding complete closure.
Mixed import profiles or plan/count pairs are rejected.

## Evidence and limits

Native and checked adapter, path, observer and command tests pass. Native
Windows command tests use a checked wide child cohort under ASCII, accented,
Japanese and supplementary-character roots. They check output bytes, failed
publication preservation and unchanged seed files. Publication suites pass
153 methods on each host; pinned manifest-runner suites pass 25 methods on
each host, with three Windows platform skips.

Paired staged proofs pass independent verification: 39 Windows and 32 Linux
artifacts match between stages three and four. Both proofs bind the same 73
source inputs. The converged Windows tools also pass 28 direct Unicode command
checks across all four path variants. Full Linux publication passes with 22
artifacts and agreement between the Cupid author and Python oracle on all 65
stage pairs. The Cupid-built manifest reader and independent artifact/input
verification pass. Paired OS acceptance rehashes 1,524 source inputs, sixteen
artifacts and 431 link inputs. Both hosts produce identical images and user
programs, and their private four-CPU disassembly/shell smokes preserve the
source images.
Installed seeds and production ownership have not changed.
