# Run a plan from the guest project directory

An editable project can keep its build plan and job files alongside its
sources. The ordinary CupidBuild worker first looks for `cupidbuild.job`
in the shell's current directory. Its job paths may be absolute or relative
to that directory. CupidBuild checks every resolved path before the first
step. Dot components and paths that exceed the VFS limit are rejected.

If the local plan is absent, the worker loads `/home/cupidbuild.job`.
That fallback still uses absolute job paths. If a local plan exists and
cannot be read or parsed, the worker reports that path and stops. The
media worker always reads `/plan/cupidbuild.job` and limits jobs to
`/plan/jobs/`.

This changes plan lookup. Each job keeps its existing source and output
path rules, including the optional source root on compile lists.
