from __future__ import annotations

import io
import hashlib
import json
import os
from pathlib import Path
import runpy
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
TOOL = runpy.run_path(str(ROOT / "engine/kernel.py"))


class KernelToolTests(unittest.TestCase):
    def test_command_transcript_output_and_status_are_always_shown(self) -> None:
        with mock.patch.object(
            TOOL["subprocess"],
            "run",
            return_value=mock.Mock(returncode=0, stdout="successful noise\n"),
        ):
            stdout = io.StringIO()
            stderr = io.StringIO()
            with mock.patch("sys.stdout", stdout), mock.patch("sys.stderr", stderr):
                TOOL["run"](["example", "success"])
            self.assertIn("$ example success", stdout.getvalue())
            self.assertIn("successful noise", stdout.getvalue())
            self.assertIn("Exit       0", stdout.getvalue())
            self.assertEqual(stderr.getvalue(), "")

        with mock.patch.object(
            TOOL["subprocess"],
            "run",
            return_value=mock.Mock(returncode=2, stdout="failure details"),
        ):
            stdout = io.StringIO()
            stderr = io.StringIO()
            with mock.patch("sys.stdout", stdout), mock.patch("sys.stderr", stderr):
                with self.assertRaisesRegex(TOOL["KernelError"], "status 2"):
                    TOOL["run"](["example", "failure"])
            self.assertIn("$ example failure", stdout.getvalue())
            self.assertIn("Exit       2", stdout.getvalue())
            self.assertIn("failure details", stderr.getvalue())
            self.assertTrue(stderr.getvalue().endswith("\n"))

    def test_termination_is_converted_to_a_cleanup_exception(self) -> None:
        with self.assertRaises(KeyboardInterrupt):
            TOOL["interrupt_for_cleanup"](15, None)

    def test_repository_path_rejects_kbuild_metacharacters(self) -> None:
        TOOL["validate_repository_path"](
            Path("/tmp/rklings-course_2026.10+local")
        )
        for character in (" ", "\t", "$", "#", "%", ":", "\\", "(", ")", ","):
            with self.subTest(character=character):
                with self.assertRaisesRegex(TOOL["KernelError"], "Kbuild"):
                    TOOL["validate_repository_path"](
                        Path(f"/tmp/rklings{character}course")
                    )


    def test_manifest_has_one_computed_release_and_verified_source(self) -> None:
        manifest = TOOL["read_manifest"]()
        paths = TOOL["layout"](manifest)
        self.assertEqual(paths["release"], "7.2.9-rklings")
        self.assertEqual(
            paths["url"],
            "https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.2.9.tar.xz",
        )
        self.assertEqual(len(manifest["source_sha256"]), 64)
        self.assertEqual(manifest["source_size"], 160392664)
        self.assertEqual(manifest["base_config"], "allnoconfig")
        self.assertEqual(manifest["config"], "vm/kernel.config")

    def test_build_identity_uses_only_explicit_artifact_inputs(self) -> None:
        globals_ = TOOL["build_identity"].__globals__
        manifest = TOOL["read_manifest"]()
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
            temporary = Path(temporary_name)
            manifest_path = temporary / "kernel.toml"
            config = temporary / "kernel.config"
            manifest_path.write_text("fixed manifest\n", encoding="ascii")
            config.write_text("CONFIG_TEST=y\n", encoding="ascii")
            inputs = {
                "MANIFEST_PATH": manifest_path,
                "tool_identity": lambda command: f"tool:{command}:1",
                "libelf_identity": lambda compiler: f"libelf:{compiler}:1",
            }
            manifest = {**manifest, "config": str(config)}
            with mock.patch.dict(globals_, inputs):
                baseline = TOOL["build_identity"](manifest)
                with mock.patch.dict(
                    globals_,
                    {"__file__": "/presentation/only/kernel.py"},
                ):
                    self.assertEqual(
                        TOOL["build_identity"](manifest),
                        baseline,
                    )

                config.write_text("CONFIG_TEST=n\n", encoding="ascii")
                self.assertNotEqual(TOOL["build_identity"](manifest), baseline)
                config.write_text("CONFIG_TEST=y\n", encoding="ascii")

                with mock.patch.dict(
                    globals_,
                    {
                        "BUILD_RECIPE_REVISION":
                            globals_["BUILD_RECIPE_REVISION"] + 1,
                    },
                ):
                    self.assertNotEqual(
                        TOOL["build_identity"](manifest),
                        baseline,
                    )

                with mock.patch.dict(
                    globals_,
                    {"tool_identity": lambda command: f"tool:{command}:2"},
                ):
                    self.assertNotEqual(
                        TOOL["build_identity"](manifest),
                        baseline,
                    )

    def test_module_sdk_digest_detects_consumed_header_changes(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
            kit = Path(temporary_name) / "kit"
            header = kit / "source/include/linux/probe.h"
            header.parent.mkdir(parents=True)
            header.write_text("#define VALUE 1\n", encoding="ascii")
            before = TOOL["sdk_digest"](kit, ("source/include",))
            header.write_text("#define VALUE 2\n", encoding="ascii")
            after = TOOL["sdk_digest"](kit, ("source/include",))
            self.assertNotEqual(before, after)

    def archive(self, members: list[tarfile.TarInfo]) -> tarfile.TarFile:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "source.tar"
        with tarfile.open(path, "w") as archive:
            for member in members:
                payload = b"x" if member.isfile() else None
                archive.addfile(member, io.BytesIO(payload) if payload else None)
        archive = tarfile.open(path, "r")
        self.addCleanup(archive.close)
        return archive

    def test_archive_accepts_only_the_pinned_source_root(self) -> None:
        regular = tarfile.TarInfo("linux-7.2.9/Makefile")
        regular.size = 1
        archive = self.archive([regular])
        self.assertEqual(
            TOOL["safe_members"](archive, "7.2.9")[0].name,
            "linux-7.2.9/Makefile",
        )

        escaping = tarfile.TarInfo("linux-7.2.9/../outside")
        escaping.size = 1
        with self.assertRaisesRegex(TOOL["KernelError"], "unsafe path"):
            TOOL["safe_members"](self.archive([escaping]), "7.2.9")

    def test_archive_rejects_absolute_and_escaping_links(self) -> None:
        absolute = tarfile.TarInfo("linux-7.2.9/link")
        absolute.type = tarfile.SYMTYPE
        absolute.linkname = "/etc/passwd"
        with self.assertRaisesRegex(TOOL["KernelError"], "absolute link"):
            TOOL["safe_members"](self.archive([absolute]), "7.2.9")

        escaping = tarfile.TarInfo("linux-7.2.9/sub/link")
        escaping.type = tarfile.SYMTYPE
        escaping.linkname = "../../outside"
        with self.assertRaisesRegex(TOOL["KernelError"], "escaping link"):
            TOOL["safe_members"](self.archive([escaping]), "7.2.9")

        hardlink = tarfile.TarInfo("linux-7.2.9/sub/link")
        hardlink.type = tarfile.LNKTYPE
        hardlink.linkname = "linux-7.2.9/Makefile"
        TOOL["safe_members"](self.archive([hardlink]), "7.2.9")

        escaping_hardlink = tarfile.TarInfo("linux-7.2.9/sub/link")
        escaping_hardlink.type = tarfile.LNKTYPE
        escaping_hardlink.linkname = "outside"
        with self.assertRaisesRegex(TOOL["KernelError"], "escaping link"):
            TOOL["safe_members"](self.archive([escaping_hardlink]), "7.2.9")

    def test_archive_rejects_duplicate_and_special_members(self) -> None:
        first = tarfile.TarInfo("linux-7.2.9/file")
        first.size = 1
        second = tarfile.TarInfo("linux-7.2.9/file")
        second.size = 1
        with self.assertRaisesRegex(TOOL["KernelError"], "unsafe path"):
            TOOL["safe_members"](self.archive([first, second]), "7.2.9")

        fifo = tarfile.TarInfo("linux-7.2.9/fifo")
        fifo.type = tarfile.FIFOTYPE
        with self.assertRaisesRegex(TOOL["KernelError"], "special file"):
            TOOL["safe_members"](self.archive([fifo]), "7.2.9")

    def test_config_contract_is_explicit_and_has_no_local_version_copy(self) -> None:
        config = TOOL["config_contract"](ROOT / "vm/kernel.config")
        self.assertEqual(config["CONFIG_X86_64"], "y")
        self.assertEqual(config["CONFIG_MODULES"], "y")
        self.assertEqual(config["CONFIG_MODULE_COMPRESS"], "n")
        self.assertEqual(config["CONFIG_SMP"], "y")
        self.assertEqual(config["CONFIG_SYSVIPC"], "y")
        self.assertEqual(config["CONFIG_BINFMT_SCRIPT"], "y")
        self.assertEqual(config["CONFIG_EXPERT"], "y")
        self.assertEqual(config["CONFIG_X86_KERNEL_IBT"], "y")
        self.assertEqual(config["CONFIG_IPV6"], "y")
        self.assertEqual(config["CONFIG_VIRTIO_NET"], "y")
        self.assertEqual(config["CONFIG_VIRTIO_MENU"], "y")
        self.assertEqual(config["CONFIG_CRYPTO_GCM"], "y")
        self.assertNotIn("CONFIG_LOCALVERSION", config)

    def test_build_environment_drops_ambient_kbuild_overrides(self) -> None:
        manifest = TOOL["read_manifest"]()
        hostile = {
            "ARCH": "arm64",
            "CROSS_COMPILE": "evil-",
            "KCONFIG_CONFIG": "/tmp/foreign",
            "LOCALVERSION": "-foreign",
            "KCFLAGS": "-DBYPASS",
            "MAKEFLAGS": "-j999",
        }
        with mock.patch.dict(os.environ, hostile, clear=False):
            environment = TOOL["build_environment"](manifest)
        self.assertEqual(environment["ARCH"], "x86_64")
        self.assertEqual(environment["LC_ALL"], "C")
        for name in hostile.keys() - {"ARCH"}:
            self.assertNotIn(name, environment)

    def test_build_transaction_can_restore_or_replace_a_previous_kit(self) -> None:
        globals_ = TOOL["begin_kit_build"].__globals__
        previous_runtime = globals_["RUNTIME"]
        try:
            with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
                globals_["RUNTIME"] = Path(temporary_name) / "runtime"
                kit = (
                    globals_["RUNTIME"]
                    / "kernel/kits/7.2.9-rklings-aaaaaaaaaaaaaaaa"
                )
                kit.mkdir(parents=True)
                (kit / "identity").write_text("old", encoding="ascii")

                backup = TOOL["begin_kit_build"](kit)
                self.assertIsNotNone(backup)
                (kit / "identity").write_text("incomplete", encoding="ascii")
                TOOL["recover_interrupted_build"](
                    TOOL["read_manifest"](),
                    {"kit": kit, "stamp": kit / "stamp.json"},
                )
                self.assertEqual((kit / "identity").read_text(encoding="ascii"), "old")

                backup = TOOL["begin_kit_build"](kit)
                (kit / "identity").write_text("new", encoding="ascii")
                TOOL["finish_kit_build"](kit, backup)
                self.assertEqual((kit / "identity").read_text(encoding="ascii"), "new")
                self.assertFalse(backup.exists())

        finally:
            globals_["RUNTIME"] = previous_runtime

    def test_pruning_removes_only_superseded_kits_for_the_same_release(self) -> None:
        globals_ = TOOL["prune_superseded_kits"].__globals__
        previous_runtime = globals_["RUNTIME"]
        try:
            with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
                runtime = Path(temporary_name) / "runtime"
                globals_["RUNTIME"] = runtime
                kits = runtime / "kernel/kits"
                kits.mkdir(parents=True)
                release = "7.2.9-rklings"
                current = kits / f"{release}-{'a' * 16}"
                superseded = kits / f"{release}-{'b' * 16}"
                other_release = kits / f"7.2.8-rklings-{'c' * 16}"
                unfinished = kits / f"{release}-{'d' * 16}.previous"
                symlink = kits / f"{release}-{'e' * 16}"
                for path in (current, superseded, other_release, unfinished):
                    path.mkdir()
                symlink.symlink_to(superseded, target_is_directory=True)

                removed = TOOL["prune_superseded_kits"](current, release)

                self.assertEqual(removed, (superseded,))
                self.assertTrue(current.is_dir())
                self.assertFalse(superseded.exists())
                self.assertTrue(other_release.is_dir())
                self.assertTrue(unfinished.is_dir())
                self.assertTrue(symlink.is_symlink())
                with self.assertRaisesRegex(
                    TOOL["KernelError"],
                    "unsafe current kernel kit",
                ):
                    TOOL["prune_superseded_kits"](
                        runtime / "outside",
                        release,
                    )
        finally:
            globals_["RUNTIME"] = previous_runtime

    def test_ensure_prunes_only_after_the_new_kit_verifies(self) -> None:
        globals_ = TOOL["ensure"].__globals__
        previous_runtime = globals_["RUNTIME"]
        try:
            with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
                runtime = Path(temporary_name) / "runtime"
                globals_["RUNTIME"] = runtime
                kit = runtime / "kernel/kits/release-aaaaaaaaaaaaaaaa"
                kit.mkdir(parents=True)
                manifest = {"source_size": 1}
                paths = {"kit": kit, "release": "release"}
                common = {
                    "recover_interrupted_build": mock.Mock(),
                    "preflight_space": mock.Mock(),
                    "build": mock.Mock(),
                    "prune_superseded_kits": mock.Mock(return_value=()),
                }
                with mock.patch.dict(globals_, common):
                    globals_["ready"] = mock.Mock(
                        side_effect=((False, "not built"), (True, "ready"))
                    )
                    with mock.patch("sys.stdout", io.StringIO()):
                        TOOL["ensure"](manifest, paths)
                    common["prune_superseded_kits"].assert_called_once_with(
                        kit,
                        "release",
                    )

                failed = {**common, "prune_superseded_kits": mock.Mock()}
                with mock.patch.dict(globals_, failed):
                    globals_["ready"] = mock.Mock(
                        side_effect=(
                            (False, "not built"),
                            (False, "artifact changed"),
                        )
                    )
                    with self.assertRaisesRegex(
                        TOOL["KernelError"],
                        "failed verification",
                    ):
                        with mock.patch("sys.stdout", io.StringIO()):
                            TOOL["ensure"](manifest, paths)
                    failed["prune_superseded_kits"].assert_not_called()
        finally:
            globals_["RUNTIME"] = previous_runtime

    def test_download_enforces_independent_size_and_digest(self) -> None:
        payload = b"abcd"
        manifest = TOOL["read_manifest"]().copy()
        manifest["source_size"] = len(payload)
        manifest["source_sha256"] = hashlib.sha256(payload).hexdigest()

        class Response(io.BytesIO):
            def __init__(self, value: bytes) -> None:
                super().__init__(value)
                self.headers = {"Content-Length": str(len(value))}

            def __enter__(self) -> "Response":
                return self

            def __exit__(self, *_arguments: object) -> None:
                self.close()

        with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
            archive = Path(temporary_name) / "cache/source.tar.xz"
            paths = {"archive": archive, "url": "https://invalid.example/source"}
            with mock.patch.object(
                TOOL["urllib"].request, "urlopen", return_value=Response(payload)
            ):
                self.assertEqual(TOOL["download"](manifest, paths), archive)
            self.assertEqual(archive.read_bytes(), payload)

            archive.unlink()
            with mock.patch.object(
                TOOL["urllib"].request, "urlopen", return_value=Response(payload + b"x")
            ):
                with self.assertRaisesRegex(TOOL["KernelError"], "size header"):
                    TOOL["download"](manifest, paths)
            self.assertFalse(archive.with_name(archive.name + ".part").exists())

    def test_readiness_requires_every_external_module_artifact(self) -> None:
        manifest = TOOL["read_manifest"]()
        globals_ = TOOL["layout"].__globals__
        previous_runtime = globals_["RUNTIME"]
        try:
            with tempfile.TemporaryDirectory(dir=ROOT) as temporary_name:
                globals_["RUNTIME"] = Path(temporary_name) / "runtime"
                paths = TOOL["layout"](manifest)
                source = paths["source"]
                build = paths["build"]
                source.mkdir(parents=True)
                build.mkdir(parents=True)
                fragment = TOOL["config_contract"](ROOT / "vm/kernel.config")
                lines = []
                for key, value in fragment.items():
                    lines.append(f"# {key} is not set" if value == "n" else f"{key}={value}")
                lines.append('CONFIG_LOCALVERSION="-rklings"')
                for key, value in TOOL["REQUIRED_FINAL_CONFIG"].items():
                    lines.append(f"{key}={value}")
                config = build / ".config"
                config.write_text("\n".join(lines) + "\n", encoding="ascii")
                for relative in TOOL["REQUIRED_BUILD_FILES"]:
                    path = build / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    if path != config:
                        path.write_bytes(b"artifact\n")
                stamp = {
                    "schema_version": 2,
                    "release": paths["release"],
                    "source_sha256": manifest["source_sha256"],
                    "build_identity": paths["build_identity"],
                    "config_sha256": TOOL["sha256_file"](config),
                    "sdk_sha256": "0" * 64,
                    "artifact_sha256": {
                        relative: TOOL["sha256_file"](build / relative)
                        for relative in TOOL["HASHED_BUILD_FILES"]
                    },
                }
                paths["stamp"].write_text(
                    json.dumps(stamp, sort_keys=True) + "\n", encoding="ascii"
                )
                completed = TOOL["subprocess"].CompletedProcess(
                    args=[], returncode=0, stdout=str(paths["release"]) + "\n", stderr=""
                )
                with (
                    mock.patch.object(TOOL["subprocess"], "run", return_value=completed),
                    mock.patch.dict(globals_, {"sdk_digest": lambda _kit: "0" * 64}),
                ):
                    self.assertEqual(TOOL["ready"](manifest, paths), (True, "ready"))
                    (build / "Module.symvers").write_bytes(b"tampered\n")
                    ready, reason = TOOL["ready"](manifest, paths)
                self.assertFalse(ready)
                self.assertEqual(reason, "build artifact changed: Module.symvers")
        finally:
            globals_["RUNTIME"] = previous_runtime


if __name__ == "__main__":
    unittest.main()
