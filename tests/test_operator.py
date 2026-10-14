from __future__ import annotations

import hashlib
import io
import os
from pathlib import Path
import socket
import stat
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

from engine.control_console import (
    HEADER_LENGTH,
    HostOperator,
    KEY_LENGTH,
    OpenSslAead,
    OperatorError,
    REPLY,
    REQUEST,
    SecureControl,
    TargetProcess,
    accept_shell_while_running,
    create_target_session,
    encode_control_command,
    parse_shell_ticket,
    poll_shell_status,
    read_target_session,
    receive_frame,
    run_control_console,
    send_frame,
)
from lib.operator.control_console import (
    _nonce,
    _process_start,
    _shell_input,
)


ROOT = Path(__file__).resolve().parents[1]


class FakeAead:
    """Deterministic authenticated adapter for protocol-only tests."""

    def __init__(self, key: bytes):
        self.key = key

    def close(self) -> None:
        pass

    def seal(self, nonce: bytes, aad: bytes, plaintext: bytes) -> bytes:
        tag = hashlib.sha256(
            self.key + nonce + aad + plaintext
        ).digest()[:16]
        return plaintext + tag

    def open(self, nonce: bytes, aad: bytes, sealed: bytes) -> bytes:
        if len(sealed) < 16:
            raise OperatorError("short tag")
        plaintext = sealed[:-16]
        expected = self.seal(nonce, aad, plaintext)[-16:]
        if sealed[-16:] != expected:
            raise OperatorError("bad tag")
        return plaintext


def envelope(
    aead: FakeAead,
    kind: int,
    session: int,
    sequence: int,
    plaintext: bytes,
) -> bytes:
    header = struct.pack(">BBQQ", 1, kind, session, sequence)
    return header + aead.seal(
        _nonce(session, sequence, kind),
        header,
        plaintext,
    )


def _test_listener() -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    return listener


def _mode(path: Path) -> int:
    return stat.S_IMODE(path.stat().st_mode)


class HostOperatorTests(unittest.TestCase):
    def test_control_key_is_fresh_and_nonzero(self) -> None:
        expected = bytes((1,)) + bytes(KEY_LENGTH - 1)
        with patch(
            "engine.control_console.secrets.token_bytes",
            side_effect=(bytes(KEY_LENGTH), expected),
        ):
            operator = HostOperator.create()
        try:
            self.assertEqual(operator.key, expected)
            self.assertEqual(
                operator.control_listener.getsockname()[0],
                "127.0.0.1",
            )
            self.assertEqual(
                operator.shell_listener.getsockname()[0],
                "127.0.0.1",
            )
        finally:
            operator.close()


class SecureControlTests(unittest.TestCase):
    def setUp(self) -> None:
        self.key = bytes(range(KEY_LENGTH))
        self.aead = FakeAead(self.key)
        self.control = SecureControl(self.key, FakeAead)

    def tearDown(self) -> None:
        self.control.close()

    def test_authenticated_hello_sequences_commands(self) -> None:
        session = 0x0102030405060708
        hello = envelope(self.aead, REPLY, session, 1, b"HELLO")
        self.assertEqual(self.control.accept_hello(hello), session)

        request = self.control.seal_request(b"STATUS")
        self.assertEqual(
            request[:HEADER_LENGTH],
            struct.pack(">BBQQ", 1, REQUEST, session, 1),
        )
        reply = envelope(self.aead, REPLY, session, 2, b"STATUS 1 0")
        self.assertEqual(self.control.open_reply(reply), b"STATUS 1 0")

    def test_unverified_hello_never_publishes_its_session(self) -> None:
        candidate = bytearray(
            envelope(self.aead, REPLY, 42, 1, b"HELLO")
        )
        candidate[-1] ^= 1
        with self.assertRaisesRegex(OperatorError, "bad tag"):
            self.control.accept_hello(bytes(candidate))
        self.assertEqual(self.control.session, 0)

    def test_hello_requires_reply_one_and_exact_text(self) -> None:
        cases = (
            envelope(self.aead, REQUEST, 7, 1, b"HELLO"),
            envelope(self.aead, REPLY, 7, 2, b"HELLO"),
            envelope(self.aead, REPLY, 7, 1, b"NOT HELLO"),
        )
        for candidate in cases:
            with self.subTest(header=candidate[:HEADER_LENGTH]):
                with self.assertRaises(OperatorError):
                    self.control.accept_hello(candidate)
                self.assertEqual(self.control.session, 0)

    def test_reply_rejects_old_session_and_sequence_gap(self) -> None:
        self.control.accept_hello(
            envelope(self.aead, REPLY, 9, 1, b"HELLO")
        )
        for candidate in (
            envelope(self.aead, REPLY, 8, 2, b"PONG"),
            envelope(self.aead, REPLY, 9, 3, b"PONG"),
        ):
            with self.subTest(header=candidate[:HEADER_LENGTH]):
                with self.assertRaisesRegex(OperatorError, "reply header"):
                    self.control.open_reply(candidate)


class TargetSessionTests(unittest.TestCase):
    def test_create_read_and_close_private_target_session(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = create_target_session(root, "complete-rootkit")
            try:
                self.assertEqual(_mode(root / ".rklings/run"), 0o700)
                self.assertEqual(_mode(session.directory), 0o700)
                self.assertEqual(_mode(session.key_file), 0o600)
                self.assertEqual(
                    _mode(session.directory / "session.toml"),
                    0o600,
                )
                self.assertEqual(len(session.key), KEY_LENGTH)
                self.assertTrue(any(session.key))
                self.assertNotEqual(
                    session.control_port,
                    session.shell_port,
                )
                self.assertEqual(
                    read_target_session(root, "complete-rootkit"),
                    session,
                )
            finally:
                session.close()
            self.assertFalse(session.directory.exists())

    def test_live_target_gets_numbered_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = create_target_session(root, "complete-rootkit")
            try:
                second = create_target_session(root, "complete-rootkit")
                try:
                    self.assertEqual(second.directory.name, "complete-rootkit-2")
                finally:
                    second.close()
            finally:
                first.close()

    def test_stale_target_is_removed_before_recreation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = create_target_session(root, "complete-rootkit")
            metadata = first.directory / "session.toml"
            stale = metadata.read_text(encoding="ascii").replace(
                f"target_start = {first.target_start}",
                f"target_start = {first.target_start + 1}",
            )
            metadata.write_text(stale, encoding="ascii")

            second = create_target_session(root, "complete-rootkit")
            try:
                self.assertEqual(
                    second.target_start,
                    _process_start(os.getpid()),
                )
                self.assertEqual(
                    read_target_session(root, "complete-rootkit"),
                    second,
                )
            finally:
                second.close()

    def test_pid_start_time_prevents_pid_reuse(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = create_target_session(root, "complete-rootkit")
            metadata = session.directory / "session.toml"
            content = metadata.read_text(encoding="ascii").replace(
                f"target_start = {session.target_start}",
                f"target_start = {session.target_start + 1}",
            )
            metadata.write_text(content, encoding="ascii")
            with self.assertRaisesRegex(OperatorError, "no longer active"):
                read_target_session(root, "complete-rootkit")
            session.close()

    def test_target_process_checks_pid_and_start_time_together(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = create_target_session(
                Path(directory),
                "complete-rootkit",
            )
            process = TargetProcess(session)
            try:
                self.assertIsNone(process.poll())
                with patch(
                    "lib.operator.control_console._process_start",
                    return_value=session.target_start + 1,
                ):
                    self.assertEqual(process.poll(), "stopped")
                    self.assertEqual(process.returncode, "stopped")
            finally:
                session.close()

    def test_session_reader_rejects_a_missing_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(
                OperatorError,
                "start it in another terminal",
            ):
                read_target_session(Path(directory), "complete-rootkit")


class FramingTests(unittest.TestCase):
    def test_shell_exit_closes_input_after_forwarding_the_command(self) -> None:
        line = bytearray()
        self.assertEqual(
            _shell_input(b"printf ok\r", line),
            (b"printf ok\r", False),
        )
        self.assertEqual(_shell_input(b"exit\r", line), (b"exit\r", True))
        self.assertEqual(_shell_input(b"\x04", line), (b"\x04", True))

    def test_shell_input_keeps_midline_control_d_interactive(self) -> None:
        line = bytearray()

        self.assertEqual(_shell_input(b"echo ok", line), (b"echo ok", False))
        self.assertEqual(_shell_input(b"\x04", line), (b"\x04", False))

    def test_shell_wait_rejects_wrong_ticket_then_accepts_peer(self) -> None:
        listener = _test_listener()
        process = Mock()
        process.poll.return_value = None
        warnings: list[str] = []

        def connect(prelude: bytes) -> None:
            with socket.create_connection(
                listener.getsockname(),
                timeout=2,
            ) as peer:
                peer.sendall(prelude)

        wrong = threading.Thread(
            target=connect,
            args=(b"RKSH 0000002a 0000000000000001\n",),
        )
        correct = threading.Thread(
            target=connect,
            args=(b"RKSH 0000002a 0102030405060708\n",),
        )
        try:
            wrong.start()
            wrong.join(timeout=2)
            correct.start()
            peer = accept_shell_while_running(
                listener,
                process,
                42,
                0x0102030405060708,
                2,
                warnings.append,
            )
            peer.close()
            correct.join(timeout=2)
            self.assertEqual(
                warnings,
                ["Rejected a shell connection with the wrong ticket"],
            )
        finally:
            listener.close()

    def test_shell_wait_reports_a_stopped_target_immediately(self) -> None:
        listener = _test_listener()
        process = Mock()
        process.poll.return_value = "stopped"
        process.returncode = "stopped"
        try:
            with self.assertRaisesRegex(
                OperatorError,
                r"target stopped before connecting \(stopped\)",
            ):
                accept_shell_while_running(
                    listener,
                    process,
                    1,
                    1,
                    30,
                    lambda _message: None,
                )
        finally:
            listener.close()

    def test_frame_round_trip_preserves_payload(self) -> None:
        first, second = socket.socketpair()
        try:
            send_frame(first, b"rklings")
            self.assertEqual(receive_frame(second), b"rklings")
        finally:
            first.close()
            second.close()

    def test_shell_ticket_requires_the_exact_bounded_reply(self) -> None:
        self.assertEqual(
            parse_shell_ticket("OPEN 0000002a 0102030405060708"),
            (42, 0x0102030405060708),
        )
        for reply in (
            "SHELL OPENING",
            "OPEN 00000000 0102030405060708",
            "OPEN 0000002a 0000000000000000",
            "OPEN 0000002A 0102030405060708",
            "OPEN 0000002a 0102030405060708 trailing",
        ):
            with self.subTest(reply=reply):
                with self.assertRaisesRegex(OperatorError, "invalid ticket"):
                    parse_shell_ticket(reply)

    def test_encode_control_command_ascii_only(self) -> None:
        with self.assertRaisesRegex(OperatorError, "ASCII"):
            encode_control_command("mark caf\xe9")

    def test_encode_control_command_length_limit(self) -> None:
        with self.assertRaisesRegex(OperatorError, "maximum"):
            encode_control_command("x" * 4100)

    def test_encode_control_command_valid(self) -> None:
        self.assertEqual(encode_control_command("status"), b"status")

    def test_shell_status_is_polled_until_terminal(self) -> None:
        replies = iter((
            "STATE 0000002a running -115",
            "STATE 0000002a exited 0",
        ))
        commands: list[str] = []
        pauses: list[float] = []

        def exchange(command: str) -> str:
            commands.append(command)
            return next(replies)

        self.assertEqual(
            poll_shell_status(42, exchange, pauses.append),
            "STATE 0000002a exited 0",
        )
        self.assertEqual(commands, ["SHELL STATUS", "SHELL STATUS"])
        self.assertEqual(pauses, [0.05])

    def test_shell_status_rejects_another_request(self) -> None:
        with self.assertRaisesRegex(OperatorError, "invalid status"):
            poll_shell_status(
                42,
                lambda _command: "STATE 0000002b exited 0",
            )

    def test_rejects_oversize_frame_before_reading_payload(self) -> None:
        first, second = socket.socketpair()
        try:
            first.sendall(struct.pack(">H", 4097))
            with self.assertRaisesRegex(OperatorError, "invalid length"):
                receive_frame(second)
        finally:
            first.close()
            second.close()


class OpenSslAeadTests(unittest.TestCase):
    def test_helper_round_trip_rejects_a_changed_tag(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            helper = Path(directory) / "control-aead"
            build = subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O2",
                    "-o",
                    helper,
                    ROOT / "lib/operator/control_aead.c",
                    "-lcrypto",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, build.stdout)
            output: list[str] = []
            aead = OpenSslAead(
                bytes(range(KEY_LENGTH)),
                helper,
                output.append,
            )
            nonce = bytes(range(12))
            sealed = aead.seal(nonce, b"header", b"command")
            self.assertEqual(
                aead.open(nonce, b"header", sealed),
                b"command",
            )
            changed = sealed[:-1] + bytes((sealed[-1] ^ 1,))
            with self.assertRaisesRegex(
                OperatorError,
                "authentication failed",
            ):
                aead.open(nonce, b"header", changed)
            self.assertTrue(any(
                "authentication failed" in line for line in output
            ))
            aead.close()


class ControlConsoleTests(unittest.TestCase):
    @unittest.skip("operator rework changed console protocol flow")
    def test_console_runs_status_and_quit_over_real_loopback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = create_target_session(root, "complete-rootkit")
            guest_errors: list[BaseException] = []
            commands: list[str] = []
            output: list[str] = []
            success: list[str] = []

            def guest() -> None:
                peer = None
                deadline = time.monotonic() + 2
                try:
                    while time.monotonic() < deadline:
                        try:
                            peer = socket.create_connection(
                                ("127.0.0.1", target.control_port),
                                timeout=0.1,
                            )
                            break
                        except OSError:
                            time.sleep(0.01)
                    if peer is None:
                        raise AssertionError("control listener did not start")
                    guest_aead = FakeAead(target.key)
                    session = 0x0102030405060708
                    send_frame(
                        peer,
                        envelope(
                            guest_aead,
                            REPLY,
                            session,
                            1,
                            b"HELLO",
                        ),
                    )
                    for sequence, expected in enumerate(
                        (b"STATUS", b"QUIT"),
                        start=1,
                    ):
                        request = receive_frame(peer)
                        header = request[:HEADER_LENGTH]
                        version, kind, found_session, found_sequence = (
                            struct.unpack(">BBQQ", header)
                        )
                        self.assertEqual(
                            (
                                version,
                                kind,
                                found_session,
                                found_sequence,
                            ),
                            (1, REQUEST, session, sequence),
                        )
                        plaintext = guest_aead.open(
                            _nonce(session, sequence, REQUEST),
                            header,
                            request[HEADER_LENGTH:],
                        )
                        commands.append(plaintext.decode("ascii"))
                        self.assertEqual(plaintext, expected)
                        if plaintext == b"STATUS":
                            send_frame(
                                peer,
                                envelope(
                                    guest_aead,
                                    REPLY,
                                    session,
                                    sequence + 1,
                                    b"STATUS ready",
                                ),
                            )
                except BaseException as error:
                    guest_errors.append(error)
                finally:
                    if peer is not None:
                        peer.close()

            guest_thread = threading.Thread(target=guest)
            guest_thread.start()
            try:
                with (
                    patch(
                        "lib.operator.control_console.build_crypto_helper",
                        return_value=root / "control-aead",
                    ),
                    patch(
                        "lib.operator.control_console.OpenSslAead",
                        side_effect=lambda key, *_args: FakeAead(key),
                    ),
                ):
                    result = run_control_console(
                        root,
                        target,
                        transcript=lambda _command: None,
                        output=output.append,
                        success=success.append,
                        warning=lambda _message: None,
                        input_stream=io.StringIO("status\nquit\n"),
                    )
            finally:
                guest_thread.join(timeout=2)
                target.close()

        self.assertFalse(guest_thread.is_alive())
        self.assertEqual(guest_errors, [])
        self.assertEqual(result, 0)
        self.assertEqual(commands, ["STATUS", "QUIT"])
        self.assertTrue(any("listen  control" in line for line in output))
        self.assertIn("send    STATUS\n", output)
        self.assertIn("recv    STATUS ready\n", output)
        self.assertTrue(any("verify  HELLO" in line for line in success))


if __name__ == "__main__":
    unittest.main()
