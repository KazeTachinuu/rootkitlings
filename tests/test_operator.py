from __future__ import annotations

import hashlib
import io
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

from engine.reference_play import (
    HEADER_LENGTH,
    HostOperator,
    KEY_LENGTH,
    OpenSslAead,
    REPLY,
    REQUEST,
    SecureControl,
    OperatorError,
    _accept_shell_while_running,
    _next_control_command,
    _nonce,
    _poll_shell_status,
    parse_shell_ticket,
    receive_frame,
    run_reference_play,
    send_frame,
)


ROOT = Path(__file__).resolve().parents[1]


class HostOperatorTests(unittest.TestCase):
    def test_control_key_is_nonzero(self):
        expected = bytes((1,)) + bytes(KEY_LENGTH - 1)
        with patch(
            "engine.reference_play.secrets.token_bytes",
            side_effect=(bytes(KEY_LENGTH), expected),
        ):
            operator = HostOperator.create()
        try:
            self.assertEqual(operator.key, expected)
        finally:
            operator.close()


class FakeAead:
    """Deterministic authenticated adapter for protocol-only unit tests."""

    def __init__(self, key: bytes):
        self.key = key

    def close(self) -> None:
        pass

    def seal(self, nonce: bytes, aad: bytes, plaintext: bytes) -> bytes:
        tag = hashlib.sha256(self.key + nonce + aad + plaintext).digest()[:16]
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


class SecureControlTests(unittest.TestCase):
    def setUp(self) -> None:
        self.key = bytes(range(KEY_LENGTH))
        self.aead = FakeAead(self.key)
        self.control = SecureControl(self.key, FakeAead)

    def tearDown(self) -> None:
        self.control.close()

    def test_authenticated_hello_sequences_commands(self):
        session = 0x0102030405060708
        hello = envelope(self.aead, REPLY, session, 1, b"HELLO")
        self.assertEqual(self.control.accept_hello(hello), session)
        self.assertEqual(self.control.session, session)

        request = self.control.seal_request(b"STATUS")
        self.assertEqual(request[:HEADER_LENGTH], struct.pack(
            ">BBQQ", 1, REQUEST, session, 1
        ))
        reply = envelope(self.aead, REPLY, session, 2, b"STATUS 1 0")
        self.assertEqual(self.control.open_reply(reply), b"STATUS 1 0")

    def test_unverified_hello_never_publishes_its_session(self):
        candidate = bytearray(envelope(self.aead, REPLY, 42, 1, b"HELLO"))
        candidate[-1] ^= 1
        with self.assertRaisesRegex(OperatorError, "bad tag"):
            self.control.accept_hello(bytes(candidate))
        self.assertEqual(self.control.session, 0)

    def test_hello_contract_rejects_wrong_direction_sequence_or_text(self):
        cases = (
            envelope(self.aead, REQUEST, 7, 1, b"HELLO"),
            envelope(self.aead, REPLY, 7, 2, b"HELLO"),
            envelope(self.aead, REPLY, 7, 1, b"NOT HELLO"),
        )
        for candidate in cases:
            with self.subTest(candidate=candidate[:HEADER_LENGTH]):
                with self.assertRaises(OperatorError):
                    self.control.accept_hello(candidate)
                self.assertEqual(self.control.session, 0)

    def test_reply_rejects_old_session_and_sequence_gap(self):
        self.control.accept_hello(envelope(self.aead, REPLY, 9, 1, b"HELLO"))
        for candidate in (
            envelope(self.aead, REPLY, 8, 2, b"PONG"),
            envelope(self.aead, REPLY, 9, 3, b"PONG"),
        ):
            with self.subTest(candidate=candidate[:HEADER_LENGTH]):
                with self.assertRaisesRegex(OperatorError, "reply header"):
                    self.control.open_reply(candidate)


class FramingTests(unittest.TestCase):
    def test_shell_wait_rejects_a_wrong_ticket_before_accepting_its_peer(self):
        listener = _test_listener()
        process = Mock()
        process.poll.return_value = None
        warnings: list[str] = []

        def connect(prelude: bytes) -> None:
            with socket.create_connection(listener.getsockname(), timeout=2) \
                    as peer:
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
            peer = _accept_shell_while_running(
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

    def test_shell_wait_reports_a_dead_guest_immediately(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        process = Mock()
        process.poll.return_value = 7
        process.returncode = 7
        try:
            with self.assertRaisesRegex(
                OperatorError,
                r"guest exited before connecting \(7\)",
            ):
                _accept_shell_while_running(
                    listener,
                    process,
                    1,
                    1,
                    30,
                    lambda _message: None,
                )
        finally:
            listener.close()

    def test_frame_round_trip_preserves_payload(self):
        first, second = socket.socketpair()
        try:
            send_frame(first, b"rootkitlings")
            self.assertEqual(receive_frame(second), b"rootkitlings")
        finally:
            first.close()
            second.close()

    def test_shell_ticket_requires_the_exact_bounded_reply(self):
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

    def test_invalid_input_warns_and_returns_the_next_command(self):
        stream = io.StringIO(
            "mark café\n"
            "mark this message is much too long\n"
            "help\n"
            "status\n"
        )
        output: list[str] = []
        warnings: list[str] = []

        self.assertEqual(
            _next_control_command(stream, output.append, warnings.append),
            ("STATUS", b"STATUS"),
        )
        self.assertEqual(len(warnings), 2)
        self.assertIn("ASCII", warnings[0])
        self.assertIn("maximum 30", warnings[1])
        self.assertTrue(any(
            line.startswith("help | status") for line in output
        ))
        self.assertEqual(output.count("rootkit> "), 4)

    def test_shell_status_is_polled_until_terminal(self):
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
            _poll_shell_status(42, exchange, pauses.append),
            "STATE 0000002a exited 0",
        )
        self.assertEqual(commands, ["SHELL STATUS", "SHELL STATUS"])
        self.assertEqual(pauses, [0.05])

    def test_shell_status_rejects_another_request(self):
        with self.assertRaisesRegex(OperatorError, "invalid status"):
            _poll_shell_status(
                42,
                lambda _command: "STATE 0000002b exited 0",
            )

    def test_rejects_oversize_frame_before_reading_payload(self):
        first, second = socket.socketpair()
        try:
            first.sendall(struct.pack(">H", 65))
            with self.assertRaisesRegex(OperatorError, "invalid length"):
                receive_frame(second)
        finally:
            first.close()
            second.close()


class OpenSslAeadTests(unittest.TestCase):
    def test_helper_round_trip_rejects_a_changed_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            helper = Path(directory) / "operator-aead"
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
                    ROOT / "engine/operator_aead.c",
                    "-lcrypto",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )
            self.assertEqual(build.returncode, 0, build.stdout)
            commands: list[list[object]] = []
            output: list[str] = []
            aead = OpenSslAead(
                bytes(range(KEY_LENGTH)),
                helper,
                commands.append,
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
            self.assertEqual(
                commands,
                [
                    [helper, "seal"],
                    [helper, "open"],
                    [helper, "open"],
                ],
            )
            self.assertTrue(any("authentication failed" in line
                                for line in output))
            aead.close()


class ReferencePlayTests(unittest.TestCase):
    def test_play_drives_the_guest_protocol_and_serializes_output(self):
        key = bytes(range(1, KEY_LENGTH + 1))
        operator = HostOperator(key, _test_listener(), _test_listener())
        guest_output_started = threading.Event()
        release_guest_output = threading.Event()
        callback_lock = threading.Lock()
        callback_active = False
        overlaps: list[str] = []
        records: list[tuple[str, object]] = []
        commands: list[str] = []
        guest_errors: list[BaseException] = []
        launch: dict[str, object] = {}

        def record(kind: str, value: object) -> None:
            nonlocal callback_active
            with callback_lock:
                if callback_active:
                    overlaps.append(kind)
                callback_active = True
            try:
                if value == "guest booted\n":
                    guest_output_started.set()
                    release_guest_output.wait(2)
                records.append((kind, value))
            finally:
                with callback_lock:
                    callback_active = False

        class FakeProcess:
            stdout = io.StringIO(
                "guest booted\nROOTKITLINGS_PLAY_READY\n"
            )
            returncode = None
            pid = 12345

            @staticmethod
            def poll():
                return None

        process = FakeProcess()

        def fake_popen(command, **kwargs):
            launch["command"] = command
            launch["kwargs"] = kwargs
            key_index = command.index("--play-key-file") + 1
            key_file = Path(command[key_index])
            launch["key"] = key_file.read_text(encoding="ascii")
            launch["mode"] = key_file.stat().st_mode & 0o777
            return process

        def guest() -> None:
            peer = None
            try:
                peer = socket.create_connection(
                    operator.control_listener.getsockname(),
                    timeout=2,
                )
                if not guest_output_started.wait(2):
                    raise AssertionError("guest output was not forwarded")
                guest_aead = FakeAead(key)
                session = 0x0102030405060708
                send_frame(
                    peer,
                    envelope(guest_aead, REPLY, session, 1, b"HELLO"),
                )
                time.sleep(0.05)
                release_guest_output.set()
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
                        (version, kind, found_session, found_sequence),
                        (1, REQUEST, session, sequence),
                    )
                    plaintext = guest_aead.open(
                        _nonce(session, sequence, REQUEST),
                        header,
                        request[HEADER_LENGTH:],
                    )
                    commands.append(plaintext.decode("ascii"))
                    if expected != b"QUIT":
                        reply = envelope(
                            guest_aead,
                            REPLY,
                            session,
                            sequence + 1,
                            b"STATUS ready",
                        )
                        send_frame(peer, reply)
            except BaseException as error:
                guest_errors.append(error)
                release_guest_output.set()
            finally:
                if peer is not None:
                    peer.close()

        guest_thread = threading.Thread(target=guest)
        guest_thread.start()
        try:
            with (
                tempfile.TemporaryDirectory() as directory,
                patch.object(HostOperator, "create", return_value=operator),
                patch(
                    "engine.reference_play._build_crypto_helper",
                    return_value=Path(directory) / "operator-aead",
                ),
                patch(
                    "engine.reference_play.OpenSslAead",
                    side_effect=lambda selected, *_args: FakeAead(selected),
                ),
                patch("engine.reference_play.subprocess.Popen",
                      side_effect=fake_popen),
                patch("engine.reference_play._stop_process") as stop,
            ):
                result = run_reference_play(
                    ROOT,
                    Path(directory) / "rootkitlings.ko",
                    "authenticated-agent",
                    ROOT / "vm/play/complete-rootkit",
                    transcript=lambda value: record("transcript", value),
                    output=lambda value: record("output", value),
                    success=lambda value: record("success", value),
                    warning=lambda value: record("warning", value),
                    input_stream=io.StringIO("status\nquit\n"),
                )
            stop.assert_called_once_with(process)
        finally:
            release_guest_output.set()
            guest_thread.join(timeout=2)
            operator.close()

        self.assertFalse(guest_thread.is_alive())
        self.assertEqual(guest_errors, [])
        self.assertEqual(result, 0)
        self.assertEqual(commands, ["STATUS", "QUIT"])
        self.assertEqual(launch["key"], key.hex() + "\n")
        self.assertEqual(launch["mode"], 0o600)
        self.assertEqual(overlaps, [])
        rendered = "".join(
            value for kind, value in records
            if kind in ("output", "success") and isinstance(value, str)
        )
        self.assertIn("guest booted", rendered)
        self.assertIn("Authenticated HELLO", rendered)
        self.assertIn("> STATUS", rendered)
        self.assertIn("< STATUS ready", rendered)


def _test_listener() -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    return listener

if __name__ == "__main__":
    unittest.main()
