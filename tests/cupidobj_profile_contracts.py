## @file
#  @brief Check CupidObj output and rejection for typed compiler profile snapshots.
import hashlib
import json
import struct
import tempfile
from pathlib import Path

from tools import cupidc_kernel_compile


REPO_ROOT = Path(__file__).resolve().parents[1]
PROFILE_SNAPSHOT_MAGIC = b"CUPROF1\0"


def _profile_snapshot(schema, profiles, inputs):
    payload = bytearray(PROFILE_SNAPSHOT_MAGIC)

    def append_u32(value):
        payload.extend(struct.pack("<I", value))

    def append_bytes(value):
        append_u32(len(value))
        payload.extend(value)

    def append_text(value):
        append_bytes(value.encode("ascii"))

    append_text(schema)
    append_u32(len(profiles))
    for name, membership in profiles:
        append_text(name)
        headers = membership["headers"]
        sources = membership["sources"]
        append_u32(len(headers))
        for path in headers:
            append_text(path)
        append_u32(len(sources))
        for path in sources:
            append_text(path)
    append_u32(len(inputs))
    for path, contents in inputs:
        append_text(path)
        append_bytes(contents)
    return bytes(payload)


class ProfileManifestTests:
    def test_profile_manifest_hashes_and_canonicalizes_a_typed_snapshot(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            snapshot = root / "doom-profile.snapshot"
            output = root / "doom-profile.json"
            profiles = [
                (
                    "doom-tree",
                    {
                        "headers": ["z.h", "a.h"],
                        "sources": [
                            "kernel/doom/z.cc",
                            "kernel/doom/a.cc",
                        ],
                    },
                ),
                (
                    "doom-compat",
                    {
                        "headers": ["a.h"],
                        "sources": ["kernel/doom/d.cc"],
                    },
                ),
            ]
            inputs = [("z.h", b"z\n"), ("a.h", b"abc")]
            snapshot.write_bytes(
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    inputs,
                )
            )

            generated = self._run_profile_manifest(
                root,
                snapshot,
                output,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertEqual(generated.stdout, "")
            self.assertEqual(generated.stderr, "")
            expected = {
                "schema": "cupid.doom-profile-inputs.v1",
                "profiles": {
                    name: sorted(membership["headers"])
                    for name, membership in profiles
                },
                "sources": {
                    name: sorted(membership["sources"])
                    for name, membership in profiles
                },
                "inputs": [
                    {
                        "path": path,
                        "bytes": len(contents),
                        "sha256": hashlib.sha256(contents).hexdigest(),
                    }
                    for path, contents in sorted(inputs)
                ],
            }
            self.assertEqual(
                output.read_bytes(),
                (json.dumps(expected, indent=2, sort_keys=True) + "\n").encode(
                    "ascii"
                ),
            )

            reordered = root / "reordered.snapshot"
            repeated = root / "repeated.json"
            reordered.write_bytes(
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    list(reversed(profiles)),
                    list(reversed(inputs)),
                )
            )
            regenerated = self._run_profile_manifest(
                root,
                reordered,
                repeated,
            )
            self.assertEqual(regenerated.returncode, 0, regenerated.stderr)
            self.assertEqual(repeated.read_bytes(), output.read_bytes())

    def test_profile_manifest_accepts_its_complete_public_boundary(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            snapshot = root / "profile-limits.snapshot"
            output = root / "profile-limits.json"
            schema = "s" * 127
            longest_profile_name = "p" * 63
            headers = [
                f"h/{'a' * 1017}{index:03d}.h"
                for index in range(512)
            ]
            sources = [
                f"s/{'b' * 1016}{index:03d}.cc"
                for index in range(512)
            ]
            profiles = [
                (
                    longest_profile_name,
                    {"headers": headers, "sources": sources},
                ),
                (
                    "limit-01",
                    {"headers": headers, "sources": sources[:484]},
                ),
                *[
                    (
                        f"limit-{index:02d}",
                        {
                            "headers": [headers[index]],
                            "sources": [sources[index]],
                        },
                    )
                    for index in range(2, 16)
                ],
            ]
            self.assertEqual(len(schema), 127)
            self.assertEqual(len(longest_profile_name), 63)
            self.assertEqual(len(profiles), 16)
            self.assertTrue(all(len(path) == 1024 for path in headers))
            self.assertTrue(all(len(path) == 1024 for path in sources))
            self.assertEqual(
                sum(
                    len(membership["headers"])
                    + len(membership["sources"])
                    for _name, membership in profiles
                ),
                2048,
            )
            snapshot.write_bytes(
                _profile_snapshot(
                    schema,
                    profiles,
                    [(path, b"") for path in headers],
                )
            )

            generated = self._run_profile_manifest(
                root,
                snapshot,
                output,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            document = json.loads(output.read_text(encoding="ascii"))
            self.assertEqual(document["schema"], schema)
            self.assertEqual(len(document["profiles"]), 16)
            self.assertEqual(len(document["sources"]), 16)
            self.assertEqual(
                len(document["profiles"][longest_profile_name]), 512
            )
            self.assertEqual(
                len(document["sources"][longest_profile_name]), 512
            )
            self.assertEqual(len(document["profiles"]["limit-01"]), 512)
            self.assertEqual(len(document["sources"]["limit-01"]), 484)
            self.assertEqual(len(document["inputs"]), 512)
            self.assertTrue(
                all(item["bytes"] == 0 for item in document["inputs"])
            )

    def test_profile_manifest_rejects_bad_snapshots_without_clobbering_output(self):
        profiles = [
            (
                "doom-tree",
                {
                    "headers": ["a.h"],
                    "sources": ["kernel/doom/a.cc"],
                },
            )
        ]
        valid_inputs = [("a.h", b"abc")]
        overlong_path = "p/" + "a" * 1023
        self.assertEqual(len(overlong_path), 1025)
        cases = (
            (
                "magic",
                b"NOTPROF\0",
                "snapshot magic is invalid",
            ),
            (
                "truncated",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    valid_inputs,
                )[:-1],
                "snapshot is truncated",
            ),
            (
                "missing-input",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["missing.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "header has no captured input",
            ),
            (
                "unreferenced-input",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    [("a.h", b"abc"), ("unused.h", b"unused")],
                ),
                "captured input is absent from every profile",
            ),
            (
                "schema-limit",
                _profile_snapshot(
                    "s" * 128,
                    profiles,
                    valid_inputs,
                ),
                "schema is invalid",
            ),
            (
                "profile-name-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "p" * 64,
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "profile name is invalid",
            ),
            (
                "path-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": [overlong_path],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    [(overlong_path, b"abc")],
                ),
                "repository path is invalid",
            ),
            (
                "case-collision",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    [("a.h", b"abc"), ("A.h", b"ABC")],
                ),
                "captured input path has a case collision",
            ),
            (
                "cross-namespace-case-collision",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["A.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    [("a.h", b"abc")],
                ),
                "path spelling has a case collision",
            ),
            (
                "profile-name-collision",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        ),
                        (
                            "DOOM-TREE",
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/b.cc"],
                            },
                        ),
                    ],
                    valid_inputs,
                ),
                "profile name has a case collision",
            ),
            (
                "profile-name-duplicate",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        ),
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": ["kernel/doom/b.cc"],
                            },
                        ),
                    ],
                    valid_inputs,
                ),
                "profile name is duplicated",
            ),
            (
                "header-duplicate",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h", "a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "header path is duplicated",
            ),
            (
                "header-case-collision",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h", "A.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "header path has a case collision",
            ),
            (
                "source-duplicate",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": [
                                    "kernel/doom/a.cc",
                                    "kernel/doom/a.cc",
                                ],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "source path is duplicated",
            ),
            (
                "source-case-collision",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": [
                                    "kernel/doom/a.cc",
                                    "kernel/doom/A.cc",
                                ],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "source path has a case collision",
            ),
            (
                "input-duplicate",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    [("a.h", b"abc"), ("a.h", b"different")],
                ),
                "captured input path is duplicated",
            ),
            (
                "unsafe-path",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["../a.h"],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    [("../a.h", b"abc")],
                ),
                "repository path is invalid",
            ),
            (
                "trailing-data",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    valid_inputs,
                )
                + b"x",
                "snapshot has trailing data",
            ),
            (
                "zero-profiles",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [],
                    valid_inputs,
                ),
                "profile count is invalid",
            ),
            (
                "zero-headers",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": [],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "header count is invalid",
            ),
            (
                "zero-sources",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": [],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "source count is invalid",
            ),
            (
                "profile-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            f"profile-{index:02d}",
                            {
                                "headers": ["a.h"],
                                "sources": [f"kernel/doom/{index:02d}.cc"],
                            },
                        )
                        for index in range(17)
                    ],
                    valid_inputs,
                ),
                "profile count is invalid",
            ),
            (
                "header-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": [
                                    f"headers/{index:03d}.h"
                                    for index in range(513)
                                ],
                                "sources": ["kernel/doom/a.cc"],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "header count is invalid",
            ),
            (
                "source-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            "doom-tree",
                            {
                                "headers": ["a.h"],
                                "sources": [
                                    f"sources/{index:03d}.cc"
                                    for index in range(513)
                                ],
                            },
                        )
                    ],
                    valid_inputs,
                ),
                "source count is invalid",
            ),
            (
                "input-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    [
                        (f"input-{index:03d}.h", b"")
                        for index in range(513)
                    ],
                ),
                "input count is invalid",
            ),
            (
                "reference-limit",
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    [
                        (
                            f"profile-{profile:02d}",
                            {
                                "headers": [
                                    f"headers/{profile:02d}/{index:03d}.h"
                                    for index in range(512)
                                ],
                                "sources": [
                                    f"sources/{profile:02d}/{index:03d}.cc"
                                    for index in range(512)
                                ],
                            },
                        )
                        for profile in range(3)
                    ],
                    valid_inputs,
                ),
                "membership reference limit is exceeded",
            ),
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            snapshot = root / "doom-profile.snapshot"
            output = root / "doom-profile.json"
            for name, payload, message in cases:
                with self.subTest(name=name):
                    snapshot.write_bytes(payload)
                    output.write_bytes(b"existing manifest")
                    generated = self._run_profile_manifest(
                        root,
                        snapshot,
                        output,
                    )
                    self.assertEqual(generated.returncode, 1)
                    self.assertIn(message, generated.stderr)
                    self.assertEqual(output.read_bytes(), b"existing manifest")

    def test_profile_manifest_sha256_padding_boundaries(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            snapshot = root / "hash-boundaries.snapshot"
            output = root / "hash-boundaries.json"
            lengths = (0, 3, 55, 56, 63, 64, 65, 129)
            inputs = [
                (f"length-{size:03d}.h", bytes(range(size)))
                for size in lengths
            ]
            profiles = [
                (
                    "hash-boundaries",
                    {
                        "headers": [path for path, _contents in inputs],
                        "sources": ["kernel/doom/hash-boundaries.cc"],
                    },
                )
            ]
            snapshot.write_bytes(
                _profile_snapshot(
                    "cupid.profile-inputs.v1",
                    profiles,
                    inputs,
                )
            )

            generated = self._run_profile_manifest(
                root,
                snapshot,
                output,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            document = json.loads(output.read_text(encoding="ascii"))
            self.assertEqual(
                {
                    item["path"]: item["sha256"]
                    for item in document["inputs"]
                },
                {
                    path: hashlib.sha256(contents).hexdigest()
                    for path, contents in inputs
                },
            )

    def test_profile_manifest_matches_the_live_doom_python_oracle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            snapshot = root / "doom-profile.snapshot"
            output = root / "doom-profile.json"
            profiles = []
            input_paths = set()
            for profile_name in ("doom-compat", "doom-tree"):
                header_paths = cupidc_kernel_compile._profile_header_paths(
                    REPO_ROOT,
                    profile_name,
                )
                source_paths = cupidc_kernel_compile._profile_source_paths(
                    REPO_ROOT,
                    profile_name,
                )
                input_paths.update(header_paths)
                profiles.append(
                    (
                        profile_name,
                        {
                            "headers": [
                                path.relative_to(REPO_ROOT).as_posix()
                                for path in header_paths
                            ],
                            "sources": [
                                path.relative_to(REPO_ROOT).as_posix()
                                for path in source_paths
                            ],
                        },
                    )
                )
            inputs = [
                (
                    path.relative_to(REPO_ROOT).as_posix(),
                    path.read_bytes(),
                )
                for path in sorted(
                    input_paths,
                    key=lambda item: item.relative_to(REPO_ROOT).as_posix(),
                )
            ]
            snapshot.write_bytes(
                _profile_snapshot(
                    "cupid.doom-profile-inputs.v1",
                    profiles,
                    inputs,
                )
            )

            generated = self._run_profile_manifest(
                root,
                snapshot,
                output,
            )

            self.assertEqual(generated.returncode, 0, generated.stderr)
            expected = (
                json.dumps(
                    cupidc_kernel_compile._profile_input_manifest(REPO_ROOT),
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ).encode("ascii")
            self.assertEqual(output.read_bytes(), expected)
