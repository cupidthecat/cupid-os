"""Build one matched current Windows stage and its behavior seed description."""
import os
from tools import bootstrap_toolchain as bootstrap


def build_behavior_cohort(case, root, directory, coordinator, *, long_paths=False):
    if os.name != 'nt':
        raise ValueError('the checked Windows cohort requires Windows')
    directory.mkdir()
    execution = bootstrap.freeze_seed_inputs(
        root / 'bootstrap/seeds/i386-windows/manifest.json', directory / 'execution')
    case.addCleanup(bootstrap.require_live_seed_inputs, execution)
    parent = bootstrap.freeze_seed_inputs(
        root / 'bootstrap/seeds/i386-linux/manifest.json', directory / 'parent-plan')
    case.addCleanup(bootstrap.require_live_seed_inputs, parent)
    linux = bootstrap._candidate_build_plan(parent.manifest['build_plan'])
    plan = bootstrap._windows_build_plan(linux, utf8=True,
        long_paths=long_paths, user_link_aliases=True)
    sources = bootstrap.freeze_source_inputs(root, linux, directory / 'source',
        windows_utf8=True, windows_long_paths=long_paths, windows_user_link_aliases=True)
    case.addCleanup(bootstrap.require_source_closures, sources, root, linux)
    stage = bootstrap._build_windows_stage(bootstrap.ToolRunner(sources.root),
        sources.root, sources.root / 'stage', execution.tools, plan,
        'checked Windows behavior fixture')
    case.assertEqual(stage.tools['cupidbuild'].read_bytes(), coordinator.read_bytes())
    behavior = bootstrap._retarget_native_windows_behavior_seed(execution,
        bootstrap._build_plan_sha256(plan), linux, sources.inventory, utf8=True,
        long_paths=long_paths, user_link_aliases=True, parent_plan_seed=parent)
    return stage, behavior
