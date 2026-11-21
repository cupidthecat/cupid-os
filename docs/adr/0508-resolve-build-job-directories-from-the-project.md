# Resolve build job directories from the project

CupidBuild passes the loaded plan's directory to its workers. A compile
list or kernel job can use `root .` for the project or name a directory
within it. CupidC still uses the logical source and include paths in the
request.

A kernel job in the same plan can use `output build` after the root line.
CupidBuild checks the resolved directory and all product paths before
running the job. It rejects empty names, traversal and paths beyond the
VFS limit. Absolute directory options still work in both project and
media jobs.
