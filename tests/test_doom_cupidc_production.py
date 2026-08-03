import hashlib
import json
import os
import re
import shutil
import subprocess
import unittest
import tempfile
from pathlib import Path
from unittest import mock

from tools import cupidc_kernel_compile as kernel_compile
from tests.test_cupidc_kernel_compile import (
    FakeExecutor,
    SEED_MANIFEST,
    _valid_elf32_object,
)


REPO_ROOT = Path(__file__).resolve().parents[1]

DOOM_COMPAT_SOURCES = (
    "kernel/doom/dglibc.cc",
    "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/doomgeneric_cupidos.cc",
)


class DoomCupidCProductionTests(unittest.TestCase):
    def _profile_fixture(self):
        temporary = tempfile.TemporaryDirectory(
            prefix="cupid-doom-production-"
        )
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name).resolve()
        arguments = kernel_compile.DOOM_COMPAT_I386_ARGUMENTS
        for index, argument in enumerate(arguments):
            if argument == "-I":
                (root / arguments[index + 1].lstrip("/")).mkdir(
                    parents=True,
                    exist_ok=True,
                )
        for relative_name in (
            kernel_compile.APPROVED_DOOM_COMPAT_SOURCES
            + kernel_compile.APPROVED_DOOM_TREE_SOURCES
        ):
            member = root / relative_name
            member.parent.mkdir(parents=True, exist_ok=True)
            member.write_text(
                "int doom_profile_fixture;\n",
                encoding="utf-8",
            )
        source = root / "kernel" / "doom" / "dglibc.cc"
        source.write_text(
            '#include "shadow.h"\nint doom_fixture;\n',
            encoding="utf-8",
        )
        lower_header = root / "kernel" / "core" / "shadow.h"
        lower_header.write_text(
            "#define DOOM_SHADOW 1\n",
            encoding="utf-8",
        )
        seed = root / "seed" / "cupidc.elf"
        seed.parent.mkdir()
        seed.write_bytes(b"seed")
        manifest = seed.parent / "manifest.json"
        manifest.write_text("{}\n", encoding="utf-8")
        output = source.with_suffix(".o")
        return root, source, lower_header, seed, manifest, output

    def _freeze_seed(self, seed):
        def freeze(_manifest, snapshot):
            return mock.Mock(
                tools={
                    "cupidc": shutil.copyfile(
                        seed,
                        snapshot / seed.name,
                    )
                }
            )

        return freeze

    def test_profiles_pin_the_complete_doom_source_cohort(self):
        self.assertEqual(
            kernel_compile.APPROVED_DOOM_COMPAT_SOURCES,
            DOOM_COMPAT_SOURCES,
        )
        tree_sources = kernel_compile.APPROVED_DOOM_TREE_SOURCES
        self.assertEqual(len(tree_sources), 80)
        self.assertEqual(len(set(tree_sources)), 80)
        self.assertEqual(tree_sources[0], "kernel/doom/i_sound_cupidos.cc")
        self.assertTrue(
            all(
                source.startswith("kernel/doom/src/")
                for source in tree_sources[1:]
            )
        )
        self.assertTrue(all(source.endswith(".cc") for source in tree_sources))

        owned_sources = set(DOOM_COMPAT_SOURCES + tree_sources)
        discovered_sources = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom").glob("*.cc")
        }
        discovered_sources.update(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom" / "src").glob("*.cc")
        )
        self.assertEqual(discovered_sources, owned_sources)
        self.assertEqual(len(owned_sources), 83)
        legacy_sources = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom").glob("*.c")
        }
        legacy_sources.update(
            path.relative_to(REPO_ROOT).as_posix()
            for path in (REPO_ROOT / "kernel" / "doom" / "src").glob("*.c")
        )
        self.assertEqual(legacy_sources, set())

    def test_live_doom_uses_the_checked_jump_and_rename_contracts(self):
        dglibc_header = (REPO_ROOT / "kernel/doom/dglibc.h").read_text(
            encoding="utf-8"
        )
        dglibc_source = (REPO_ROOT / "kernel/doom/dglibc.cc").read_text(
            encoding="utf-8"
        )
        compatibility = (
            REPO_ROOT / "kernel/doom/dglibc_compat.h"
        ).read_text(encoding="utf-8")
        system_header = (
            REPO_ROOT / "kernel/doom/src/i_system.h"
        ).read_text(encoding="utf-8")
        system_source = (
            REPO_ROOT / "kernel/doom/src/i_system.cc"
        ).read_text(encoding="utf-8")
        platform_source = (
            REPO_ROOT / "kernel/doom/doomgeneric_cupidos.cc"
        ).read_text(encoding="utf-8")
        game_source = (
            REPO_ROOT / "kernel/doom/src/g_game.cc"
        ).read_text(encoding="utf-8")
        main_source = (
            REPO_ROOT / "kernel/doom/src/d_main.cc"
        ).read_text(encoding="utf-8")
        config_source = (
            REPO_ROOT / "kernel/doom/src/m_config.cc"
        ).read_text(encoding="utf-8")
        libc_stubs = (
            REPO_ROOT / "kernel/doom/doom_libc_stubs.cc"
        ).read_text(encoding="utf-8")
        vfs_header = (REPO_ROOT / "kernel/fs/vfs.h").read_text(
            encoding="utf-8"
        )
        vfs_source = (REPO_ROOT / "kernel/fs/vfs.cc").read_text(
            encoding="utf-8"
        )
        homefs_source = (REPO_ROOT / "kernel/fs/homefs.cc").read_text(
            encoding="utf-8"
        )
        homefs_header = (REPO_ROOT / "kernel/fs/homefs.h").read_text(
            encoding="utf-8"
        )
        ramfs_source = (REPO_ROOT / "kernel/fs/ramfs.cc").read_text(
            encoding="utf-8"
        )
        blockcache_header = (
            REPO_ROOT / "kernel/fs/blockcache.h"
        ).read_text(encoding="utf-8")
        blockcache_source = (
            REPO_ROOT / "kernel/fs/blockcache.cc"
        ).read_text(encoding="utf-8")
        fat16_source = (REPO_ROOT / "kernel/fs/fat16.cc").read_text(
            encoding="utf-8"
        )
        fat16_vfs_source = (
            REPO_ROOT / "kernel/fs/fat16_vfs.cc"
        ).read_text(encoding="utf-8")
        vfs_helpers_source = (
            REPO_ROOT / "kernel/fs/vfs_helpers.cc"
        ).read_text(encoding="utf-8")

        self.assertIn("__attribute__((returns_twice))", dglibc_header)
        self.assertGreaterEqual(
            dglibc_header.count("__attribute__((noreturn))"), 3
        )
        self.assertIn('"    leal  4(%esp), %ecx\\n"', dglibc_source)
        self.assertNotIn('"    movl  %esp, 16(%eax)\\n"', dglibc_source)
        self.assertIn(
            "n > 0xffffffffu - (uint32_t)sizeof(dg_alloc_hdr_t)",
            dglibc_source,
        )
        self.assertIn("n > 0xffffffffu / sz", dglibc_source)
        self.assertLess(
            dglibc_source.index(
                "f = (DG_FILE *)dg_malloc((uint32_t)sizeof(struct DG_FILE))"
            ),
            dglibc_source.index("fd = vfs_open(path, flags)"),
        )
        self.assertIn("#define O_CREAT   0x0100", compatibility)
        self.assertIn("#define O_APPEND  0x0400", compatibility)
        self.assertIn("#define EXDEV  18", compatibility)

        self.assertEqual(
            system_header.count("__attribute__((noreturn))"), 2
        )
        self.assertRegex(
            system_source,
            r"#if ORIGCODE\s+SDL_Quit\(\);\s+#endif\s+exit\(0\);",
        )
        self.assertIn("void I_ResetExitState(void)", system_source)
        self.assertIn("exit_funcs = NULL;", system_source)
        self.assertIn("already_quitting = false;", system_source)
        self.assertEqual(platform_source.count("I_ResetExitState();"), 2)
        self.assertIn(
            "extern void  I_ResetExitState(void);", platform_source
        )
        self.assertIn("I_Quit();", platform_source)
        self.assertNotIn("I_Endoom(endoom);\n\n\texit(0);", main_source)
        self.assertIn(
            "defined(ORIGCODE) || defined(DOOM_PORT_CUPIDOS)",
            config_source,
        )
        self.assertIn(".tmp.cfg", config_source)
        self.assertIn("static boolean ParseConfigLine", config_source)
        self.assertNotIn("fscanf(", config_source)
        self.assertIn("errno == ERANGE", config_source)
        self.assertIn("ResetCollectionDefaults", config_source)
        self.assertIn("initial_captured", config_source)
        self.assertIn("int M_ConfigFilesystemTest(void)", config_source)
        self.assertIn("cutoff = limit / (unsigned long)base", libc_stubs)
        self.assertIn("return negative ? DG_LONG_MIN : DG_LONG_MAX", libc_stubs)
        self.assertIn(
            "if (fclose(save_stream) != 0 || savegame_error)",
            game_source,
        )
        self.assertNotIn("remove(savegame_file);", game_source)
        self.assertIn(
            "if (rename(temp_savegame_file, savegame_file) != 0)",
            game_source,
        )

        self.assertIn("int (*rename)(void *fs_private", vfs_header)
        self.assertIn("VFS_EXDEV", vfs_header)
        self.assertIn("VFS_EBUSY", vfs_header)
        self.assertIn(
            "return old_mount == new_mount ? VFS_ENOSYS : VFS_EXDEV",
            vfs_source,
        )
        self.assertNotIn("copy incomplete", vfs_source)
        self.assertIn("static int homefs_rename_op", homefs_source)
        self.assertIn("if (rc < 0) {", homefs_source)
        self.assertIn(
            "destination->parent = destination_parent", homefs_source
        )
        self.assertIn("homefs_checked_add_u32", homefs_source)
        self.assertIn("HOMEFS_MAX_DEPTH", homefs_source)
        self.assertIn("homefs_valid_name_bytes", homefs_source)
        self.assertIn("node->open_count++", homefs_source)
        self.assertIn("return VFS_EBUSY;", homefs_source)
        self.assertIn("return failure;", homefs_source)
        self.assertIn("return load_status;", homefs_source)
        self.assertIn("if (g_homefs) return VFS_EBUSY;", homefs_source)
        self.assertIn("int homefs_batch_begin(void);", homefs_header)
        self.assertIn("int homefs_batch_end(void);", homefs_header)
        self.assertIn(
            "if (fs->batch_depth > 0u) return VFS_OK;", homefs_source
        )
        self.assertIn(
            "if (fs->batch_depth > 0u) return VFS_EBUSY;", homefs_source
        )
        self.assertIn(
            "g_homefs->batch_depth == 0xffffffffu", homefs_source
        )
        self.assertIn(
            "!g_homefs || g_homefs->batch_depth == 0u", homefs_source
        )
        self.assertIn(
            "publish_status = homefs_batch_end();", dglibc_source
        )
        self.assertIn(
            "[PASS] dglibc HomeFS batch boundary", dglibc_source
        )
        self.assertIn("while (acquired < 16)", dglibc_source)
        self.assertIn("denied = vfs_open(path, O_WRONLY | O_CREAT)", dglibc_source)
        self.assertIn("for (int i = 0; i < acquired; i++)", dglibc_source)
        self.assertIn("fat16_open_checked(path, &file)", homefs_source)
        self.assertIn(
            "fat16_reserve_file(HOMEFS_CONTAINER_NAME)", homefs_source
        )
        self.assertIn(
            "fat16_release_file_reservation(HOMEFS_CONTAINER_NAME)",
            homefs_source,
        )
        self.assertIn("rec.name_len > size - pos", homefs_source)
        self.assertIn("rec.size > size - pos", homefs_source)
        self.assertNotIn("pos + rec.name_len > size", homefs_source)
        self.assertNotIn("pos + rec.size > size", homefs_source)
        self.assertIn("static ramfs_node_t *ramfs_existing_parent", ramfs_source)
        self.assertIn("node->open_count++", ramfs_source)
        self.assertIn("return VFS_EBUSY;", ramfs_source)
        self.assertIn(
            "if (node->open_count > 0u)", ramfs_source
        )
        ramfs_write_start = ramfs_source.index("static int ramfs_write(")
        ramfs_seek_start = ramfs_source.index("static int ramfs_seek(")
        ramfs_write = ramfs_source[ramfs_write_start:ramfs_seek_start]
        self.assertLess(
            ramfs_write.index("if (end > RAMFS_MAX_DATA)"),
            ramfs_write.index("/* Grow buffer if needed */"),
        )
        self.assertIn(
            "if (new_pos > RAMFS_MAX_DATA) return VFS_ENOSPC;",
            ramfs_source,
        )
        self.assertNotIn(
            "ramfs_mkdirs(fs->root, new_path", ramfs_source
        )

        self.assertIn("int blockcache_flush_all(void);", blockcache_header)
        self.assertIn("int blockcache_sync(void);", blockcache_header)
        self.assertIn("return status;", blockcache_source)
        self.assertIn(
            "return home_status < 0 ? home_status : cache_status;",
            blockcache_source,
        )
        self.assertEqual(
            blockcache_source.count(
                "blkdev_read(cache.device, lba, 1, loaded)"
            ),
            2,
        )
        self.assertNotIn(
            "blkdev_read(cache.device, lba, 1, entry->data)",
            blockcache_source,
        )
        self.assertEqual(
            blockcache_source.count("entry->dirty = 0;"), 3
        )
        self.assertIn("int blockcache_failure_selftest(void)", blockcache_source)
        self.assertIn("memset(buffer, 0x5a, SECTOR_SIZE)", blockcache_source)
        self.assertIn(
            "blockcache_failure_victim_is_safe(&entries[0], &test)",
            blockcache_source,
        )
        homefs_flush_start = homefs_source.index("static int homefs_flush(")
        homefs_read_start = homefs_source.index(
            "static int homefs_read_fat_file("
        )
        homefs_flush = homefs_source[homefs_flush_start:homefs_read_start]
        self.assertNotIn("blockcache_flush_all()", homefs_flush)
        self.assertIn("rc = fat16_write_reserved_file(", homefs_flush)

        publish_start = fat16_source.index(
            "static int fat16_publish_directory_sector("
        )
        open_start = fat16_source.index("int fat16_open_checked(")
        read_start = fat16_source.index("int fat16_read(")
        write_start = fat16_source.index("int fat16_write_file(")
        delete_start = fat16_source.index("int fat16_delete_file(")
        mkdir_start = fat16_source.index("int fat16_mkdir(")
        publish_helper = fat16_source[publish_start:write_start]
        checked_open = fat16_source[open_start:read_start]
        write_file = fat16_source[write_start:delete_start]
        delete_file = fat16_source[delete_start:mkdir_start]
        mkdir_file = fat16_source[mkdir_start:]
        self.assertIn("fat16_get_dir_cluster_checked", checked_open)
        self.assertIn("fat16_read_fat_entry_checked", checked_open)
        self.assertNotIn("cur = fat16_read_fat_entry(cur)", checked_open)
        self.assertIn("return FAT16_OPEN_NO_HANDLES;", checked_open)
        self.assertIn("directory_lba", checked_open)
        self.assertIn(
            "count > file->file_size - file->position", fat16_source
        )
        self.assertNotIn(
            "file->position + count > file->file_size", fat16_source
        )
        self.assertIn("if (blockcache_sync() == 0)", publish_helper)
        self.assertIn(
            "blockcache_write(lba, original) == 0 && "
            "blockcache_sync() == 0",
            publish_helper,
        )
        self.assertEqual(
            write_file.count("fat16_publish_directory_sector("), 4
        )
        self.assertGreaterEqual(
            write_file.count(
                "fat16_release_unpublished_chain(first_cluster);"
            ),
            12,
        )
        self.assertIn(
            "fat16_release_unpublished_pair(first_cluster, c);",
            write_file,
        )
        self.assertEqual(
            write_file.count(
                "if (entries[i].attributes & FAT_ATTR_DIRECTORY) {"
            ),
            2,
        )
        self.assertNotIn(
            "fat16_free_chain(entries[i].first_cluster)", write_file
        )
        self.assertNotIn("blockcache_sync();", write_file)
        self.assertLess(
            write_file.index("int was_end_marker"),
            write_file.index("memset(entry"),
        )
        self.assertLess(
            write_file.index("fat16_publish_directory_sector("),
            write_file.rindex("fat16_free_chain(old_cluster);"),
        )
        self.assertEqual(
            write_file.count("fat16_directory_entry_is_open("), 2
        )
        self.assertIn("fat16_reserved_write_depth == 0", write_file)
        self.assertEqual(
            delete_file.count("fat16_publish_deleted_sector("), 2
        )
        self.assertLess(
            delete_file.index("fat16_publish_deleted_sector("),
            delete_file.index("fat16_free_chain(target_cluster);"),
        )
        self.assertIn(
            "if (blockcache_sync() != 0)", mkdir_file
        )
        self.assertIn(
            "fat16_publish_directory_sector(", mkdir_file
        )
        self.assertLess(
            mkdir_file.index("blockcache_sync()"),
            mkdir_file.index("fat16_publish_directory_sector("),
        )

        fat_open_start = fat16_vfs_source.index(
            "static int fat16_vfs_open("
        )
        fat_close_start = fat16_vfs_source.index(
            "static int fat16_vfs_close("
        )
        fat_read_start = fat16_vfs_source.index(
            "static int fat16_vfs_read("
        )
        fat_open = fat16_vfs_source[fat_open_start:fat_close_start]
        fat_close = fat16_vfs_source[fat_close_start:fat_read_start]
        self.assertIn("fat16_file_is_reserved(name)", fat_open)
        self.assertNotIn("fat16_delete_file(", fat_open)
        self.assertNotIn("fat16_delete_file(", fat_close)
        self.assertIn("previous entry retained", fat_close)
        self.assertIn("FAT16_OPEN_NO_HANDLES", fat16_vfs_source)
        self.assertIn("fat16_vfs_canonical_path", fat_open)
        self.assertIn("status = VFS_EBUSY;", fat_close)
        self.assertIn("return status;", fat_close)
        self.assertIn(
            "count > 0xffffffffu - h->cursor", fat16_vfs_source
        )
        self.assertEqual(
            vfs_helpers_source.count("int close_rc = vfs_close(fd);"),
            2,
        )
        self.assertIn(
            "int dst_close_rc = vfs_close(dst_fd);", vfs_helpers_source
        )
        self.assertIn(
            "strcmp(canonical_src, canonical_dest) == 0",
            vfs_helpers_source,
        )
        self.assertIn("while (written < (uint32_t)r)", vfs_helpers_source)
        self.assertGreaterEqual(
            vfs_helpers_source.count("return VFS_EIO;"), 4
        )
        copy_file = vfs_helpers_source[
            vfs_helpers_source.index("int vfs_copy_file(") :
        ]
        self.assertIn("st.type != VFS_TYPE_FILE", copy_file)
        self.assertLess(
            copy_file.index("if (dst_close_rc < 0)"),
            copy_file.rindex("return (int)total;"),
        )

    def test_checked_seed_compiles_g_game_subobject_pointer_initializers(self):
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        seed = SEED_MANIFEST.parent / "cupidc.elf"
        if os.name == "nt" and shutil.which("wsl") is None:
            self.skipTest("WSL is not available")
        if os.name != "nt" and not os.access(seed, os.X_OK):
            self.skipTest("checked seed is not executable")

        with tempfile.TemporaryDirectory(
            prefix=".doom-g-game-",
            dir=REPO_ROOT,
        ) as temporary:
            output = Path(temporary) / "g_game.o"
            kernel_compile.compile_kernel_source(
                REPO_ROOT,
                REPO_ROOT / "kernel" / "doom" / "src" / "g_game.cc",
                output,
                profile="doom-tree",
            )
            image = output.read_bytes()

        self.assertEqual(
            (len(image), hashlib.sha256(image).hexdigest()),
            (
                52004,
                "51aff2138ff2ee51bae9cc18e1dcc415567c6be1699ef0ef6f1ed2b009c30df1",
            ),
        )

    def test_profiles_build_the_exact_compiler_argument_vectors(self):
        compat = kernel_compile.build_compile_arguments(
            "/kernel/doom/dglibc.cc",
            "/kernel/doom/dglibc.o",
            "/frozen/repository",
            profile="doom-compat",
        )
        tree = kernel_compile.build_compile_arguments(
            "/kernel/doom/src/am_map.cc",
            "/kernel/doom/src/am_map.o",
            "/frozen/repository",
            profile="doom-tree",
        )
        self.assertEqual(
            compat,
            (
                "-c",
                "/kernel/doom/dglibc.cc",
                "-o",
                "/kernel/doom/dglibc.o",
                *kernel_compile.DOOM_COMPAT_I386_ARGUMENTS,
                "--root",
                "/frozen/repository",
            ),
        )
        self.assertEqual(
            tree,
            (
                "-c",
                "/kernel/doom/src/am_map.cc",
                "-o",
                "/kernel/doom/src/am_map.o",
                *kernel_compile.DOOM_TREE_I386_ARGUMENTS,
                "--root",
                "/frozen/repository",
            ),
        )
        self.assertEqual(compat.count("--doom-compat"), 1)
        self.assertNotIn("DEBUG=1", compat)
        self.assertIn("/kernel/doom/src", compat)
        self.assertIn("/kernel/doom/src/include_stubs", compat)
        self.assertIn(
            'DEFAULT_SAVEGAMEDIR="/home/doom/"',
            tree,
        )
        self.assertIn("DOOM_PORT_CUPIDOS=1", tree)
        self.assertEqual(
            tree[tree.index("-include") + 1],
            "/kernel/doom/dglibc_compat.h",
        )

    def test_wrapper_profiles_exactly_match_the_audited_make_profiles(self):
        audit = json.loads(
            (
                REPO_ROOT
                / "docs"
                / "bootstrap"
                / "audits"
                / "active-build.json"
            ).read_text(encoding="utf-8")
        )
        profiles = {
            profile["name"]: profile
            for profile in audit["contracts"][
                "c_preprocessor_translation_units"
            ]["profiles"]
        }
        for name, arguments in (
            ("DOOM_COMPAT_I386", kernel_compile.DOOM_COMPAT_I386_ARGUMENTS),
            ("DOOM_TREE_I386", kernel_compile.DOOM_TREE_I386_ARGUMENTS),
        ):
            with self.subTest(profile=name):
                includes = []
                definitions = []
                forced = []
                index = 0
                self.assertEqual(
                    arguments[:3],
                    ("--gnu", "--doom-compat", "--freestanding"),
                )
                index = 3
                while index < len(arguments):
                    option = arguments[index]
                    value = arguments[index + 1]
                    if option == "-I":
                        includes.append(value)
                    elif option == "-D":
                        macro, replacement = value.split("=", 1)
                        definitions.append((macro, replacement))
                    elif option == "-include":
                        forced.append(value)
                    else:
                        self.fail(
                            f"unexpected {name} wrapper option: {option}"
                        )
                    index += 2
                profile = profiles[name]
                self.assertEqual(
                    includes,
                    [entry["path"] for entry in profile["include_roots"]],
                )
                audited_definitions = [
                    (action["name"], action["replacement"])
                    for action in profile["macro_actions"]
                    if action["name"] != "__SIZEOF_POINTER__"
                ]
                self.assertEqual(definitions, audited_definitions)
                self.assertEqual(forced, profile["forced_includes"])
                pointer_action = next(
                    action
                    for action in profile["macro_actions"]
                    if action["name"] == "__SIZEOF_POINTER__"
                )
                self.assertEqual(pointer_action["replacement"], "4")

    def test_cross_profile_source_is_rejected_before_seed_execution(self):
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "source is outside the approved doom-tree CupidC cohort",
        ):
            kernel_compile.compile_kernel_source(
                REPO_ROOT,
                REPO_ROOT / "kernel" / "doom" / "dglibc.cc",
                REPO_ROOT / "kernel" / "doom" / "dglibc.o",
                profile="doom-tree",
            )

    def test_doom_profile_freezes_the_source_and_complete_header_space(self):
        inputs = kernel_compile._kernel_input_paths(
            REPO_ROOT,
            "kernel/doom/dglibc.cc",
            "doom-compat",
        )
        relative = {
            path.relative_to(REPO_ROOT).as_posix() for path in inputs
        }
        self.assertIn("kernel/doom/dglibc.cc", relative)
        self.assertIn("drivers/serial.h", relative)
        self.assertIn("kernel/core/types.h", relative)
        self.assertIn("kernel/doom/src/doomdef.h", relative)
        self.assertIn(
            "kernel/doom/src/include_stubs/stdint.h",
            relative,
        )
        self.assertIn("toolchain/ctool.h", relative)
        self.assertEqual(
            {
                path
                for path in relative
                if Path(path).suffix == ".cc"
            },
            {"kernel/doom/dglibc.cc"},
        )
        self.assertTrue(
            all(
                Path(path).suffix in {".cc", ".h", ".inc"}
                for path in relative
            )
        )

    def test_make_header_dependencies_equal_the_frozen_profile_space(self):
        result = subprocess.run(
            ("make", "--print-data-base", "--dry-run", "FORCE"),
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(
            r"^DOOM_CUPIDC_HEADERS := (.*)$",
            result.stdout,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        make_headers = set(match.group(1).split())
        frozen_headers = {
            path.relative_to(REPO_ROOT).as_posix()
            for path in kernel_compile._profile_header_paths(
                REPO_ROOT,
                "doom-compat",
            )
        }
        self.assertEqual(make_headers, frozen_headers)

    def test_doom_compile_runs_from_one_frozen_profile_snapshot(self):
        root, source, header, seed, manifest, output = (
            self._profile_fixture()
        )
        captured = {}

        class ClosureExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                compiler_root = Path(
                    arguments[arguments.index("--root") + 1]
                )
                for path in (source, header):
                    relative = path.relative_to(root).as_posix()
                    captured[relative] = (
                        compiler_root / relative
                    ).read_bytes()
                return super().run(executable, arguments, timeout)

        executor = ClosureExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            kernel_compile.compile_kernel_source(
                root,
                source,
                output,
                manifest=manifest,
                executor=executor,
                profile="doom-compat",
            )

        self.assertEqual(
            captured,
            {
                "kernel/doom/dglibc.cc": source.read_bytes(),
                "kernel/core/shadow.h": header.read_bytes(),
            },
        )
        self.assertEqual(output.read_bytes(), _valid_elf32_object())
        arguments = executor.calls[0][1]
        self.assertIn("--doom-compat", arguments)
        self.assertNotEqual(
            Path(arguments[arguments.index("--root") + 1]),
            root,
        )

    def test_doom_header_drift_preserves_the_existing_object(self):
        root, source, header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")

        class DriftingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                header.write_text(
                    "#define DOOM_SHADOW 2\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = DriftingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "doom-compat profile inputs changed while compiling "
                "kernel/doom/dglibc.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_header_membership_drift_preserves_the_existing_object(self):
        root, source, _header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")
        shadowing_header = root / "kernel" / "shadow.h"

        class AddingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                shadowing_header.write_text(
                    "#define DOOM_SHADOW 3\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = AddingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "doom-compat profile inputs changed while compiling "
                "kernel/doom/dglibc.cc",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_source_membership_drift_preserves_the_existing_object(self):
        root, source, _header, seed, manifest, output = (
            self._profile_fixture()
        )
        output.write_bytes(b"existing object")
        unlisted_source = root / "kernel" / "doom" / "src" / "added.cc"

        class AddingExecutor(FakeExecutor):
            def run(self, executable, arguments, timeout):
                unlisted_source.write_text(
                    "int unlisted_doom_source;\n",
                    encoding="utf-8",
                )
                return super().run(executable, arguments, timeout)

        executor = AddingExecutor(root, payload=_valid_elf32_object())
        with mock.patch.object(
            kernel_compile,
            "freeze_seed_inputs",
            side_effect=self._freeze_seed(seed),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "Doom profile source membership differs from the "
                "approved cohort",
            ):
                kernel_compile.compile_kernel_source(
                    root,
                    source,
                    output,
                    manifest=manifest,
                    executor=executor,
                    profile="doom-compat",
                )
        self.assertEqual(output.read_bytes(), b"existing object")

    def test_doom_profile_rejects_a_nested_symlink(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "linked-headers"
        try:
            link.symlink_to(
                root / "kernel" / "core",
                target_is_directory=True,
            )
        except OSError as error:
            self.skipTest(f"directory symlink unavailable: {error}")
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    def test_doom_profile_rejects_a_header_symlink(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "linked.h"
        try:
            link.symlink_to(header)
        except OSError as error:
            self.skipTest(f"file symlink unavailable: {error}")
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    @unittest.skipUnless(os.name == "nt", "NTFS junction test")
    def test_doom_profile_rejects_a_nested_junction(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        link = root / "kernel" / "doom" / "junction-headers"
        result = subprocess.run(
            (
                "cmd",
                "/c",
                "mklink",
                "/J",
                str(link),
                str(root / "kernel" / "core"),
            ),
            text=True,
            capture_output=True,
            timeout=30,
            check=False,
        )
        if result.returncode != 0:
            self.skipTest(f"junction unavailable: {result.stderr}")
        self.addCleanup(
            lambda: link.rmdir() if link.exists() else None
        )
        self.assertTrue(link.is_junction())
        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            "CupidC profile input may not be a link or junction",
        ):
            kernel_compile._profile_header_paths(
                root,
                "doom-compat",
            )

    def test_profile_input_manifest_records_every_approved_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        document = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(
            document["sources"],
            {
                "doom-compat": list(
                    kernel_compile.APPROVED_DOOM_COMPAT_SOURCES
                ),
                "doom-tree": list(
                    kernel_compile.APPROVED_DOOM_TREE_SOURCES
                ),
            },
        )
        self.assertEqual(
            sum(len(members) for members in document["sources"].values()),
            83,
        )

    def test_profile_input_manifest_rejects_a_legacy_c_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        legacy = (
            root
            / "kernel"
            / "doom"
            / "src"
            / "unlisted"
            / "legacy.c"
        )
        legacy.parent.mkdir()
        legacy.write_text("int legacy_doom_source;\n", encoding="utf-8")
        output = root / "build" / "doom-inputs.json"

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            r"unlisted kernel/doom/src/unlisted/legacy\.c",
        ):
            kernel_compile.write_profile_input_manifest(root, output)
        self.assertFalse(output.exists())

    def test_profile_input_manifest_changes_only_with_the_header_space(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        first = output.read_bytes()
        first_time = output.stat().st_mtime_ns
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)
        self.assertEqual(output.stat().st_mtime_ns, first_time)

        original_header = header.read_bytes()
        header.write_text(
            "#define DOOM_SHADOW 9\n",
            encoding="utf-8",
        )
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertNotEqual(output.read_bytes(), first)

        header.write_bytes(original_header)
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

        added = root / "kernel" / "doom" / "added.h"
        added.write_text("#define ADDED 1\n", encoding="utf-8")
        kernel_compile.write_profile_input_manifest(root, output)
        changed = output.read_bytes()
        self.assertNotEqual(changed, first)

        added.unlink()
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

    def test_normal_make_object_rejects_a_renamed_doom_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        shutil.copyfile(REPO_ROOT / "Makefile", root / "Makefile")
        tools = root / "tools"
        tools.mkdir()
        for name in (
            "bootstrap_toolchain.py",
            "cupidc_kernel_compile.py",
            "kernel_cupidc_frontier.py",
        ):
            shutil.copyfile(REPO_ROOT / "tools" / name, tools / name)
        seed_root = root / "bootstrap" / "seeds" / "i386-linux"
        seed_root.mkdir(parents=True)
        (seed_root / "manifest.json").write_text("{}\n", encoding="utf-8")
        for name in (
            "cupidasm.elf",
            "cupidc.elf",
            "cupiddis.elf",
            "cupidld.elf",
            "cupidobj.elf",
        ):
            (seed_root / name).write_bytes(b"seed")

        manifest_target = "build/bootstrap/doom-cupidc-inputs.json"
        first = subprocess.run(
            ("make", manifest_target),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(
            first.returncode,
            0,
            msg=first.stderr or first.stdout,
        )
        manifest = root / manifest_target
        published = manifest.read_bytes()

        source = root / "kernel" / "doom" / "src" / "am_map.cc"
        source.rename(source.with_name("am_map-renamed.cc"))
        second = subprocess.run(
            ("make", "kernel/doom/src/d_event.o"),
            cwd=root,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertNotEqual(second.returncode, 0)
        self.assertIn(
            "CupidC profile source is unavailable: "
            "kernel/doom/src/am_map.cc",
            second.stderr + second.stdout,
        )
        self.assertEqual(manifest.read_bytes(), published)

    def test_doom_profile_rejects_an_incomplete_include_space(self):
        with tempfile.TemporaryDirectory(
            prefix="cupid-doom-profile-"
        ) as temporary:
            root = Path(temporary)
            source = root / "kernel" / "doom" / "dglibc.cc"
            source.parent.mkdir(parents=True)
            source.write_text("int source;\n", encoding="utf-8")
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "CupidC profile include root is unavailable: /kernel",
            ):
                kernel_compile._kernel_input_paths(
                    root,
                    "kernel/doom/dglibc.cc",
                    "doom-compat",
                )

    def test_makefile_uses_explicit_checked_profiles_for_every_doom_root(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertNotIn("$(CC) $(CFLAGS_DOOM)", makefile)
        self.assertNotIn("$(CC) $(CFLAGS_DOOM_TREE)", makefile)
        self.assertNotIn("kernel/doom/src/*.c)", makefile)
        self.assertIn("kernel/doom/src/*.cc)", makefile)
        self.assertEqual(
            makefile.count("--profile doom-compat"),
            len(DOOM_COMPAT_SOURCES),
        )
        self.assertEqual(makefile.count("--profile doom-tree"), 2)
        self.assertIn(
            "kernel/doom/src/%.o: kernel/doom/src/%.cc",
            makefile,
        )
        self.assertIn(
            "kernel/doom/src/include_stubs/*/*.h",
            makefile,
        )
        self.assertIn("toolchain/tests/*.inc", makefile)

    def test_make_dry_run_keeps_host_tools_out_of_every_doom_object(self):
        sources = DOOM_COMPAT_SOURCES + (
            kernel_compile.APPROVED_DOOM_TREE_SOURCES
        )
        targets = tuple(
            Path(source).with_suffix(".o").as_posix() for source in sources
        )
        environment = os.environ.copy()
        marker = "DOOM_HOST_TOOL_MUST_NOT_RUN"
        for variable in (
            "CC",
            "CXX",
            "CPP",
            "HOSTCC",
            "HOSTCXX",
            "ASM",
            "AS",
            "LD",
            "AR",
            "NM",
            "OBJCOPY",
        ):
            environment[variable] = marker
        result = subprocess.run(
            ("make", "--dry-run", "--always-make", *targets),
            cwd=REPO_ROOT,
            env=environment,
            text=True,
            capture_output=True,
            timeout=60,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=result.stderr or result.stdout,
        )
        self.assertNotIn(marker, result.stdout)
        self.assertEqual(
            result.stdout.count("--profile doom-compat"),
            len(DOOM_COMPAT_SOURCES),
        )
        self.assertEqual(
            result.stdout.count("--profile doom-tree"),
            len(kernel_compile.APPROVED_DOOM_TREE_SOURCES),
        )
        for source in sources:
            self.assertIn(f"--source {source}", result.stdout)


if __name__ == "__main__":
    unittest.main()
