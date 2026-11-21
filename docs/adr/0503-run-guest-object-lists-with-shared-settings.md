# Run guest object lists with shared settings

The source-media plan carried a separate request for each compilation and
wrapped asset. Most of those requests repeated the same options. The plan
needed its own ISO, and the assets needed a fourth mount.

Let CupidBuild run ordered object lists. A `compile-list` starts with the
compiler options and a line containing `sources`. An `asset-list` starts
with the wrapping mode and an absolute asset directory. Both formats then
list source and output paths in pairs. An asset path is relative to the
asset directory and retains its original identity for symbol generation.

The worker checks the complete list, including its output paths, before
starting the first item. Each item uses the existing compiler or object
worker and publishes independently. A tool failure stops the list and
leaves the earlier products available.

The host keeps source order and groups adjacent entries with the same
settings. The normal 430-object replay now uses 19 requests and three
mounts. Assets live under `/plan/assets` on the plan ISO.
