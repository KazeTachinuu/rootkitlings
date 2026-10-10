from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from engine.guest_protocol import GuestProtocolError, parse_guest_check


ROOT = Path(__file__).resolve().parents[1]


class GuestProtocolTests(unittest.TestCase):
    def test_script_runs_directly_without_shadowing_the_standard_library(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            serial_log = Path(directory) / "serial.log"
            serial_log.write_text(
                "ROOTKITLINGS_CHECK_BEGIN\n"
                "visible output\n"
                "ROOTKITLINGS_RESULT=pass\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, ROOT / "engine/guest_protocol.py", serial_log],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "visible output\n")

    def test_parses_exact_crlf_delimited_pass_envelope(self) -> None:
        result = parse_guest_check(
            "firmware output\r\n"
            "ROOTKITLINGS_CHECK_BEGIN\r\n"
            "[*] lifecycle\r\n"
            "[+] unloaded\r\n"
            "ROOTKITLINGS_RESULT=pass\r\n"
            "reboot output\r\n"
        )
        self.assertTrue(result.passed)
        self.assertEqual(result.status, 0)
        self.assertEqual(result.output, "[*] lifecycle\n[+] unloaded\n")

    def test_parses_failure_without_exposing_protocol_records(self) -> None:
        result = parse_guest_check(
            "ROOTKITLINGS_CHECK_BEGIN\n"
            "[x] assertion failed\n"
            "ROOTKITLINGS_RESULT=fail:7\n"
        )
        self.assertFalse(result.passed)
        self.assertEqual(result.status, 7)
        self.assertEqual(result.output, "[x] assertion failed\n")

    def test_rejects_missing_duplicate_and_out_of_order_records(
        self,
    ) -> None:
        invalid = (
            "ROOTKITLINGS_RESULT=pass\n",
            "ROOTKITLINGS_CHECK_BEGIN\nROOTKITLINGS_RESULT=passenger\n",
            "ROOTKITLINGS_CHECK_BEGIN\nROOTKITLINGS_RESULT=pass\n"
            "ROOTKITLINGS_RESULT=pass\n",
            "ROOTKITLINGS_CHECK_BEGIN\nROOTKITLINGS_RESULT=passenger\n"
            "ROOTKITLINGS_RESULT=pass\n",
            "ROOTKITLINGS_RESULT=pass\nROOTKITLINGS_CHECK_BEGIN\n",
        )
        for text in invalid:
            with self.subTest(text=text), self.assertRaises(GuestProtocolError):
                parse_guest_check(text)


if __name__ == "__main__":
    unittest.main()
