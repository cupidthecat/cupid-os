# Plan the kernel from the installed source layout

CupidBuild on the guest already runs the tools and links the kernel.
An installed checkout can now keep its kernel build description in
`bootstrap/guest/kernel.layout`. The layout records the source role for
each pass-one object, in link order. It also carries the compiler
profiles and installation inputs used to produce those objects.

The `project-kernel` step reads the layout from HomeFS. It groups sources
by profile or asset mode and sends the existing workers in-memory
requests. The final request carries the original pass-one order.
The project keeps its kernel products under `build` and can leave
intermediate objects in RAMFS.

Keep the host recipe comparison while Make drives the supported host
build. The check detects changes to object order, source roles, option
sets, and installation inputs. The checkout supplies the layout and its
source files to the running OS. The read-only source-media entry keeps
its ordered job format.
