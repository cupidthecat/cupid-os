"""Separate historical fixture shapes from the current producer closure."""
import copy

from tools import bootstrap_toolchain as bootstrap


OWNER_SOURCE_NAMES = (
    "cupidbuild_source_path", "cupidbuild_source_file", "cupidbuild_source_stage",
    "cupidbuild_disk_integer", "cupidbuild_disk_options",
    "cupidbuild_disk_input_discovery", "path_encoding",
)


def historical_disk_manifest(document):
    result = copy.deepcopy(document)
    plan = result["build_plan"]
    plan["sources"] = [
        {"name": name, "path": path, "gnu_extensions": gnu}
        for name, path, gnu in (*bootstrap.EXPECTED_SOURCES, *bootstrap.PROMOTED_SOURCES)
    ]
    plan["links"]["cupidbuild"] = list(bootstrap.PROMOTED_CUPIDBUILD_LINK)
    plan["links"]["cupidobj"] = list(bootstrap.PROMOTED_CUPIDOBJ_LINK)
    result["build_plan_sha256"] = bootstrap._build_plan_sha256(plan)
    return result


def historical_disk_snapshot(snapshot):
    later = {"toolchain/" + name + ".h" for name in OWNER_SOURCE_NAMES
             if name != "path_encoding"}
    later.add("toolchain/hosted/i386-linux/include/cupid_runtime.h")
    return {name: row for name, row in snapshot.items() if name not in later}


def observer_workers_for_seed(plan_sha256, source_input_count):
    imports = dict(bootstrap._promoted_windows_imports(
        "cupidbuild", plan_sha256, source_input_count))
    return "CreateThread" in imports["KERNEL32.dll"]
