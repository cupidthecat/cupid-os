# ADR 0478: Bind observer workers to an explicit Windows profile

Installed Windows seeds validate exact historical import lists and build-plan
digests. Adding CreateThread to the shared CupidBuild import constant would
change those profiles before a replacement cohort has been qualified.

The Windows plan builder now accepts an explicit Boolean `observer_workers`
selection, defaulting to false. It adds one CreateThread import and the
qualified three-word `observer_thread` adapter only to CupidBuild. The typed
profile classifier checks the complete import map, exact adapter path, single
assembly row and link role. The existing three-field profile query remains
available to historical callers; stage construction uses the complete typed
profile for both linking and PE import validation.

Source capture has a corresponding explicit selection. It hashes and freezes
the adapter and retains the selection through live and frozen revalidation.
Missing input and invalid selection fail before creating the freeze directory.
The ordinary and UTF-8 callback/heap contracts now build the production adapter.

This is preparation for source-owner adoption. Current producer entry points
still select the legacy profile; installed seed files and normal image
ownership do not change. The source-owner modules, producer and consumer
qualification, and publication handoff remain subsequent work. See the
[profile record](../bootstrap/OBSERVER-WORKER-PROFILE.md).
