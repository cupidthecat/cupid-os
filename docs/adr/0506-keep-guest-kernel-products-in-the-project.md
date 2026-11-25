# Keep guest kernel products in the project

Add an optional `output /home/project/build` line to the kernel job.
Put it after the optional `root` line and before the pass-one object
paths. CupidBuild checks every product path before assembling the boot
sector. It rejects a pass-one input that would be overwritten by the
generated symbol object.

The target directory must exist. Use HomeFS for production kernel files
and disk prefixes, which exceed RAMFS's per-file limit. A kernel job
without an output line writes its products to `/home`.

The installed-tree replay creates `/home/project/build` and places all
persistent products there. Pass-one objects can still go to RAMFS when
requested. The media plan retains its existing output paths.
