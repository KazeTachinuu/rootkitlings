"""Parse the private result envelope around guest check output."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys


BEGIN = "RKLINGS_CHECK_BEGIN"
RESULT = re.compile(r"RKLINGS_RESULT=(pass|fail:([1-9][0-9]{0,2}))\Z")


class GuestProtocolError(ValueError):
    """The guest did not emit one complete, ordered result envelope."""


class GuestRebootedError(GuestProtocolError):
    """The guest rebooted mid-check without producing a result."""


@dataclass(frozen=True)
class GuestCheckResult:
    passed: bool
    status: int
    output: str


def parse_guest_check(text: str) -> GuestCheckResult:
    lines = text.replace("\r\n", "\n").replace("\r", "\n").splitlines()
    begin_indices = [index for index, line in enumerate(lines) if line == BEGIN]
    if len(begin_indices) != 1:
        raise GuestProtocolError("expected exactly one guest check begin record")
    begin = begin_indices[0]
    result_records = [
        (index, line)
        for index, line in enumerate(lines)
        if line.startswith("RKLINGS_RESULT=")
    ]
    if not result_records:
        raise GuestRebootedError("guest rebooted without producing a result")
    if len(result_records) != 1:
        raise GuestProtocolError("expected exactly one guest check result record")
    result_index, result_line = result_records[0]
    match = RESULT.fullmatch(result_line)
    if match is None:
        raise GuestProtocolError("guest check result record is malformed")
    if result_index <= begin:
        raise GuestProtocolError("guest check result precedes its begin record")
    output = "\n".join(lines[begin + 1:result_index])
    if output:
        output += "\n"
    if match.group(1) == "pass":
        return GuestCheckResult(True, 0, output)
    status = int(match.group(2))
    if status > 255:
        raise GuestProtocolError("guest check status is outside the shell exit range")
    return GuestCheckResult(False, status, output)


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        print("usage: guest_protocol.py SERIAL_LOG", file=sys.stderr)
        return 2
    try:
        text = Path(arguments[0]).read_text(encoding="utf-8", errors="replace")
        result = parse_guest_check(text)
    except GuestRebootedError as error:
        print(error, file=sys.stderr)
        return 3
    except (OSError, GuestProtocolError) as error:
        print(error, file=sys.stderr)
        return 2
    stream = sys.stdout if result.passed else sys.stderr
    stream.write(result.output)
    stream.flush()
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
