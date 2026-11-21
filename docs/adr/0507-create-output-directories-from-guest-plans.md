# Create output directories from guest plans

An installed project starts with its source tree and jobs. Let the plan
create output directories before it runs the tools. Write `mkdir` on one
line and a project-relative directory on the next.

CupidBuild checks every path before executing the plan. The directory step
belongs to plans loaded from the working directory. It rejects absolute
paths and dot components. An existing directory counts as success. An
existing file or missing parent stops the plan. List parents before their
children when creating more than one level.

The HomeFS fixtures start with a `mkdir` step for `build`. The terminal
enters the project and runs CupidBuild.
