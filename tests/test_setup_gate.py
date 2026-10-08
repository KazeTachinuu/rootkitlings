from pathlib import Path
import os
import tempfile
import unittest

from lib.setup_gate import (
    SetupGateError,
    SETUP_STAMP_PATH,
    SetupStamp,
    boundary_hash,
    read_setup_stamp,
    setup_status,
    write_setup_stamp,
)


class SetupGateTests(unittest.TestCase):
    def test_canonical_stamp_path(self) -> None:
        self.assertEqual(SETUP_STAMP_PATH.as_posix(), ".rootkitlings/setup.toml")

    def stamp(self, **changes) -> SetupStamp:
        values = {
            "policy_version": "policy-v1",
            "course_revision": "course-v1",
            "kernel_release": "6.12.1-test",
            "boundary_hash": boundary_hash({"policy": "none", "probe": b"diskless"}),
        }
        values.update(changes)
        return SetupStamp(**values)

    def test_boundary_hash_is_order_independent_but_type_and_framing_sensitive(self) -> None:
        first = boundary_hash({"b": b"c", "a": "bc"})
        self.assertEqual(first, boundary_hash({"a": "bc", "b": b"c"}))
        self.assertNotEqual(first, boundary_hash({"a": b"bc", "b": b"c"}))
        self.assertNotEqual(first, boundary_hash({"a": "b", "b": b"cc"}))

    def test_round_trip_is_mode_0600_and_exact_schema(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "setup.toml"
            expected = self.stamp()
            write_setup_stamp(path, expected, checks_passed=True)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(read_setup_stamp(path), expected)
            self.assertEqual(setup_status(path, expected).reason, "ready")
            self.assertFalse(any(item.name.startswith(".setup.toml.") for item in path.parent.iterdir()))

    def test_writer_refuses_failed_checks_without_creating_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "setup.toml"
            with self.assertRaisesRegex(SetupGateError, "successful caller checks"):
                write_setup_stamp(path, self.stamp(), checks_passed=False)
            self.assertFalse(path.exists())

    def test_status_reports_each_stale_dimension(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "setup.toml"
            actual = self.stamp()
            write_setup_stamp(path, actual, checks_passed=True)
            cases = {
                "policy-version-changed": self.stamp(policy_version="policy-v2"),
                "course-revision-changed": self.stamp(course_revision="course-v2"),
                "kernel-release-changed": self.stamp(kernel_release="other"),
                "boundary-changed": self.stamp(boundary_hash="0" * 64),
            }
            for reason, expected in cases.items():
                with self.subTest(reason=reason):
                    status = setup_status(path, expected)
                    self.assertFalse(status.ready)
                    self.assertEqual(status.reason, reason)

    def test_missing_and_invalid_stamps_have_precise_status(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "setup.toml"
            self.assertEqual(setup_status(path, self.stamp()).reason, "missing")
            path.write_text("authorized = true\n")
            status = setup_status(path, self.stamp())
            self.assertFalse(status.ready)
            self.assertEqual(status.reason, "invalid: setup stamp fields do not match schema v1")

    def test_exact_fields_and_types_are_enforced(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "setup.toml"
            valid = (
                'schema_version = 1\nauthorized = true\npolicy_version = "p"\n'
                'course_revision = "c"\nkernel_release = "k"\n'
                f'boundary_hash = "{"0" * 64}"\n'
            )
            for mutation in (
                valid + "extra = true\n",
                valid.replace("schema_version = 1", 'schema_version = "1"'),
                valid.replace("authorized = true", "authorized = 1"),
                valid.replace('policy_version = "p"', "policy_version = 1"),
            ):
                path.write_text(mutation)
                with self.assertRaises(SetupGateError):
                    read_setup_stamp(path)

    def test_symlink_file_and_parent_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target"
            target.write_text("not a stamp")
            linked_file = root / "setup.toml"
            os.symlink(target, linked_file)
            with self.assertRaisesRegex(SetupGateError, "safely open"):
                read_setup_stamp(linked_file)
            with self.assertRaisesRegex(SetupGateError, "regular file or absent"):
                write_setup_stamp(linked_file, self.stamp(), checks_passed=True)

            real_parent = root / "runtime"
            real_parent.mkdir()
            linked_parent = root / "linked-runtime"
            os.symlink(real_parent, linked_parent)
            with self.assertRaisesRegex(SetupGateError, "parent must not be a symlink"):
                write_setup_stamp(
                    linked_parent / "setup.toml",
                    self.stamp(),
                    checks_passed=True,
                )


if __name__ == "__main__":
    unittest.main()
