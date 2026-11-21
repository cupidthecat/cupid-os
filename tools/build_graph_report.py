## @file
#  @brief Render the build audit as JSON and Markdown.
"""Render a collected build audit without reading source files."""

import collections
import json


def render_json(audit: dict[str, object]) -> str:
    return json.dumps(audit, indent=2, sort_keys=True) + "\n"


def _markdown_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_markdown(audit: dict[str, object]) -> str:
    source_counts = collections.Counter(
        str(source["language"]) for source in audit["sources"]
    )
    cohort_counts = collections.Counter(
        str(source["cohort"]) for source in audit["sources"]
    )
    cohort_lines: collections.Counter[str] = collections.Counter()
    for source in audit["sources"]:
        if source["lines"] is not None:
            cohort_lines[str(source["cohort"])] += int(source["lines"])
    unreachable_counts = collections.Counter(
        str(source["classification"])
        for source in audit["unreachable_sources"]
    )
    tool_counts: collections.Counter[str] = collections.Counter()
    all_builds = [audit["build"], *audit["supplemental_builds"]]
    all_transforms = [
        transform for build in all_builds for transform in build["transforms"]
    ]
    for transform in all_transforms:
        tool_counts.update(str(tool) for tool in transform["tools"])
    feature_counts: collections.Counter[str] = collections.Counter()
    feature_occurrences: collections.Counter[str] = collections.Counter()
    for feature in audit["features"]:
        parts = str(feature["id"]).split(".")
        group = ".".join(parts[:2]) if len(parts) > 1 else parts[0]
        feature_counts[group] += 1
        feature_occurrences[group] += int(feature["occurrences"])

    lines = [
        "# Active build and source audit",
        "",
        "This file is generated deterministically by "
        "`tools/build_graph_audit.py` from the supported Make graph and source tree.",
        "",
        "## Scope",
        "",
        f"- Root Make target: `{audit['build']['root_target']}`",
        "- Supplemental builds: "
        + (
            ", ".join(
                f"`{build['directory']}:{build['root_target']}`"
                for build in audit["supplemental_builds"]
            )
            or "none"
        ),
        f"- Active source inputs: {audit['summary']['active_sources']}",
        f"- Unreachable source-like files: {audit['summary']['unreachable_sources']}",
        f"- Reachable output transforms: {audit['summary']['transforms']}",
        f"- Distinct feature requirements: {audit['summary']['features']}",
        "- Make conditionals use the canonical `OS=Windows_NT` graph and the "
        "C locale fixes wildcard order on every host. Direct Linux build "
        "tests cover the Linux execution branch.",
        "- The `TempleOS/` reference tree is excluded.",
        "- Source and control-file SHA-256 values use canonical LF text bytes.",
        "",
        "Generated C translation units are recorded as reachable build inputs but have "
        "no source hash or lexical features; their content is owned by the recorded "
        "generator transform.",
        "",
        "## Active language inputs",
        "",
        "| Language | Files |",
        "| --- | ---: |",
    ]
    lines.extend(
        f"| `{_markdown_cell(language)}` | {count} |"
        for language, count in sorted(source_counts.items())
    )
    lines.extend(
        [
            "",
            "## Source cohorts",
            "",
            "| Cohort | Files | Checked-source lines |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(cohort)}` | {count} | {cohort_lines[cohort]} |"
        for cohort, count in sorted(cohort_counts.items())
    )
    lines.extend(
        [
            "",
            "## Supported build roots",
            "",
            "| Directory | Root target | Transforms | Include paths |",
            "| --- | --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{build['directory']}` | `{build['root_target']}` | "
        f"{len(build['transforms'])} | {len(build['include_search_paths'])} |"
        for build in all_builds
    )
    lines.extend(
        [
            "",
            "## Current output ownership",
            "",
            "| Tool interface | Reachable transforms |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(tool)}` | {count} |"
        for tool, count in sorted(tool_counts.items())
    )
    lines.extend(
        [
            "",
            "## Feature inventory",
            "",
            "| Feature family | Distinct requirements | Lexical/build occurrences |",
            "| --- | ---: | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(group)}` | {count} | {feature_occurrences[group]} |"
        for group, count in sorted(feature_counts.items())
    )
    lines.extend(
        [
            "",
            "The JSON companion records stable feature IDs, occurrence counts, files, "
            "and representative source locations.",
            "",
        ]
    )
    if audit["abi"] is not None:
        abi = audit["abi"]
        linker = abi["linker_script"]
        lines.extend(
            [
                "## ABI and object contract",
                "",
                "| Property | Required value |",
                "| --- | --- |",
                f"| Architecture | `{abi['architecture']}` |",
                f"| Data model | `{abi['data_model']}` |",
                f"| Endianness | `{abi['endianness']}` |",
                f"| Calling convention | `{abi['calling_convention']}` |",
                f"| Object interchange | `{abi['object_interchange']}` |",
                f"| Required relocations | `{', '.join(abi['required_relocations'])}` |",
                f"| Stack alignment | {abi['stack_alignment_bytes']} bytes |",
                "",
            ]
        )
        if linker is not None:
            lines.extend(
                [
                    f"`{linker['path']}` has SHA-256 `{linker['sha256']}` and uses "
                    f"{', '.join(f'`{feature}`' for feature in linker['features'])}.",
                    "It is referenced by linker flags but is not a declared Make "
                    "prerequisite."
                    if not linker["declared_make_prerequisite"]
                    else "It is also a declared Make prerequisite.",
                    "",
                ]
            )
    lines.extend(
        [
            "## Source-driven capability priority",
            "",
            "| Rank | Capability | Source evidence |",
            "| ---: | --- | ---: |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` - {_markdown_cell(item['title'])} | "
        f"{item['source_count']} |"
        for item in audit["roadmap"]["capability_priorities"]
    )
    lines.extend(
        [
            "",
            "## Source-cohort migration order",
            "",
            "| Rank | Cohort step | Files | Rationale |",
            "| ---: | --- | ---: | --- |",
        ]
    )
    lines.extend(
        f"| {item['rank']} | `{item['id']}` | {item['source_count']} | "
        f"{_markdown_cell(item['rationale'])} |"
        for item in audit["roadmap"]["source_cohort_order"]
    )
    lines.extend(
        [
            "",
            "## Unreachable source classification",
            "",
            "| Classification | Files |",
            "| --- | ---: |",
        ]
    )
    lines.extend(
        f"| `{_markdown_cell(classification)}` | {count} |"
        for classification, count in sorted(unreachable_counts.items())
    )
    lines.extend(
        [
            "",
            "An exact content match does not by itself prove semantic duplication; "
            "path-sensitive compatibility headers remain removal-blocked.",
            "",
            "| Path | Language | Classification | Lines | Evidence |",
            "| --- | --- | --- | ---: | --- |",
        ]
    )
    for source in audit["unreachable_sources"]:
        duplicates = source.get("duplicate_of", [])
        semantic_relations = [
            relation
            for relation in source.get("relations", [])
            if relation["kind"] != "exact_content_match"
        ]
        if semantic_relations:
            evidence = ", ".join(
                f"{relation['kind']}: `{relation['path']}`"
                for relation in semantic_relations
            )
        elif duplicates:
            shown = ", ".join(f"`{path}`" for path in duplicates[:3])
            if len(duplicates) > 3:
                shown += f" (+{len(duplicates) - 3} more)"
            evidence = f"content match: {shown}"
        else:
            evidence = str(source["reason"])
        lines.append(
            f"| `{source['path']}` | `{source['language']}` | "
            f"`{source['classification']}` | {source['lines']} | "
            f"{_markdown_cell(evidence)} |"
        )
    lines.extend(
        [
            "",
            "## Audit contracts",
            "",
            "| Contract | Status | Detail |",
            "| --- | --- | --- |",
        ]
    )
    if audit["contracts"]:
        for name, contract in sorted(audit["contracts"].items()):
            if "tool_images" in contract:
                link_counts = ", ".join(
                    f"{entry['tool']}={entry['objects']}"
                    for entry in contract["link_objects"]
                )
                detail = (
                    f"{contract['tool_c_sources']} tool C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['tool_images']} tools "
                    f"({link_counts}); "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_tool_images']} tool images; "
                    f"{contract['success_behavior_cases']} success and "
                    f"{contract['failure_behavior_cases']} failure cases; "
                    f"{contract['platform']}"
                )
            elif "compiler_c_sources" in contract:
                detail = (
                    f"{contract['compiler_c_sources']} compiler C sources "
                    f"({contract['strict_c_sources']} strict, "
                    f"{contract['gnu_c_sources']} GNU); "
                    f"{contract['link_objects']} linked objects; "
                    f"{contract['compared_c_objects']} C objects and "
                    f"{contract['compared_startup_objects']} startup object "
                    f"compared across stages; "
                    f"{contract['compared_compiler_images']} compiler images; "
                    f"{contract['behavior_cases']} behavior cases; "
                    f"{contract['platform']}"
                )
            elif "tracked_translation_units" in contract:
                profile_counts = ", ".join(
                    f"{profile['name']}={profile['tracked_translation_units']}"
                    for profile in contract["profiles"]
                )
                detail = (
                    f"{contract['tracked_translation_units']} tracked + "
                    f"{contract['generated_translation_units']} generated "
                    f"translation units ({profile_counts}); "
                    f"{contract['include_only_fragments']} include-only, "
                    f"{contract['delivered_non_root_headers']} non-root headers; "
                    f"{contract['deferred_hosted_translation_units']} hosted "
                    f"deferred ({contract['deferred_external_header_units']} "
                    "external, "
                    f"{contract['deferred_hermetic_units']} hermetic)"
                )
            elif "tracked_c_sources" in contract:
                detail = (
                    f"{contract['tracked_c_sources']} tracked .c sources; "
                    f"{contract['active_tracked_c_sources']} active; "
                    f"{contract['cupidc_owned_tracked_c_sources']} owned by "
                    "CupidC; "
                    f"{contract['unreachable_tracked_c_sources']} unreachable; "
                    f"{contract['tracked_cupid_c_sources']} tracked .cc "
                    f"sources; {contract['active_tracked_cupid_c_sources']} "
                    "active with independent CupidC evidence; "
                    f"{contract['unreachable_tracked_cupid_c_sources']} "
                    "unreachable"
                )
            elif "cupidasm_owned_sources" in contract:
                detail = (
                    f"{contract['active_sources']} active assembly sources; "
                    f"{contract['cupidasm_owned_sources']} CupidASM-owned; "
                    f"{contract['toolchain_startup_sources']} Toolchain "
                    "startup; "
                    f"{contract['other_owned_sources']} other-owned; "
                    f"{contract['ownerless_sources']} ownerless; "
                    f"{len(contract['explicit_classifications'])} explicit "
                    "host-only classifications"
                )
            elif "expression_occurrences" in contract:
                detail = (
                    f"{contract['expression_occurrences']} conditional expressions "
                    f"({contract['if_occurrences']} #if, "
                    f"{contract['elif_occurrences']} #elif); "
                    f"{contract['unique_expressions']} normalized expressions; "
                    f"{contract['directive_expression_pairs']} "
                    "directive/expression pairs"
                )
            elif "named_line_occurrences" in contract:
                directive_word = (
                    "directive"
                    if contract["named_line_occurrences"] == 1
                    else "directives"
                )
                source_word = (
                    "file" if contract["source_files"] == 1 else "files"
                )
                detail = (
                    f"{contract['named_line_occurrences']} named #line "
                    f"{directive_word} ({contract['direct_line_occurrences']} "
                    f"direct, {contract['pp_token_line_occurrences']} pp-token; "
                    f"{contract['filename_occurrences']} filename); "
                    f"{contract['numeric_marker_occurrences']} numeric markers; "
                    f"{contract['source_files']} source {source_word}; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pp_token_operand_occurrences" in contract:
                detail = (
                    f"{contract['include_occurrences']} C include operands "
                    f"({contract['direct_quoted_occurrences']} quoted, "
                    f"{contract['direct_angle_occurrences']} angle, "
                    f"{contract['pp_token_operand_occurrences']} pp-token); "
                    f"{contract['source_files']} source files; "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "exe_occurrences" in contract:
                detail = (
                    f"{contract['block_occurrences']} Cupid #exe blocks "
                    f"({contract['ordinary_marker_occurrences']} #, "
                    f"{contract['digraph_marker_occurrences']} %:); "
                    "max conditional depth "
                    f"{contract['max_conditional_depth']}"
                )
            elif "pragma_occurrences" in contract:
                detail = (
                    f"{contract['pragma_occurrences']} pragmas "
                    f"({contract['once_occurrences']} once, "
                    f"{contract['pack_push_occurrences']} pack pushes, "
                    f"{contract['pack_pop_occurrences']} pack pops); "
                    "pack balanced: "
                    f"{'yes' if contract['pack_balanced'] else 'no'}; "
                    f"max pack depth {contract['max_pack_depth']}"
                )
            else:
                missing = contract.get("missing_link_inputs", [])
                detail = (
                    f"{contract.get('linked_objects', 0)} linked objects; "
                    f"{contract.get('declared_artifacts', 0)} declared artifacts; "
                    f"{len(missing)} missing"
                )
            lines.append(
                f"| `{_markdown_cell(name)}` | `{contract['status']}` | "
                f"{_markdown_cell(detail)} |"
            )
    else:
        lines.append("| _none declared_ | `not_applicable` | n/a |")
    lines.extend(
        [
            "",
            "## Interpretation limits",
            "",
            "- Feature occurrences are comment/string-masked lexical evidence, not a "
            "substitute for a compiler AST or executed semantic tests.",
            "- Include reachability follows checked Make include paths, forced "
            "includes, "
            "quoted/angle C includes, and `%include`; the conditional contract records "
            "normalized source expressions while evaluation remains a "
            "compiler-contract responsibility.",
            "- Named `#line` pp-token operands are classified before macro expansion; "
            "the CupidC corpus harness owns expansion and semantic validation.",
            "- Relocation kinds and ABI values are required interchange contracts; "
            "per-object relocation counts are recorded in the chronological bootstrap log.",
            "- `not_reached` means absent from the supported roots recorded above, not "
            "automatically safe to delete.",
        ]
    )
    return "\n".join(lines) + "\n\n"
