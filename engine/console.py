"""Terminal presentation and logging for Rootkitlings commands."""

from __future__ import annotations

import logging
import os
import shlex
import shutil
import sys
from typing import TextIO


_RESET = "\x1b[0m"
_STYLES = {
    "accent": "\x1b[1;34m",
    "command": "\x1b[1;36m",
    "dim": "\x1b[2m",
    "error": "\x1b[1;31m",
    "success": "\x1b[1;32m",
    "warning": "\x1b[1;33m",
}


def _color_enabled(stream: TextIO) -> bool:
    return (
        "NO_COLOR" not in os.environ
        and os.environ.get("TERM") != "dumb"
        and bool(getattr(stream, "isatty", lambda: False)())
    )


def _styled(text: str, style: str, stream: TextIO) -> str:
    if not _color_enabled(stream):
        return text
    return f"{_STYLES[style]}{text}{_RESET}"


class _LevelFilter(logging.Filter):
    def __init__(self, minimum: int, maximum: int | None = None) -> None:
        super().__init__()
        self.minimum = minimum
        self.maximum = maximum

    def filter(self, record: logging.LogRecord) -> bool:
        return record.levelno >= self.minimum and (
            self.maximum is None or record.levelno <= self.maximum
        )


class _ConsoleHandler(logging.Handler):
    """A logging handler that follows redirected stdout and stderr."""

    def __init__(
        self,
        target: str,
        minimum: int,
        maximum: int | None = None,
    ) -> None:
        super().__init__()
        self.target = target
        self.addFilter(_LevelFilter(minimum, maximum))

    def emit(self, record: logging.LogRecord) -> None:
        stream = getattr(sys, self.target)
        try:
            message = record.getMessage()
            kind = getattr(record, "console_kind", "message")
            if kind == "success":
                message = f"{_styled('[+]', 'success', stream)} {message}"
            elif kind == "warning":
                message = f"{_styled('[!]', 'warning', stream)} {message}"
            elif kind == "error":
                message = f"{_styled('[x]', 'error', stream)} {message}"
            elif kind == "progress":
                message = f"{_styled('[*]', 'accent', stream)} {message}"
            stream.write(message + "\n")
            stream.flush()
        except Exception:
            self.handleError(record)


def _logger(name: str) -> logging.Logger:
    result = logging.getLogger(name)
    result.handlers.clear()
    result.setLevel(logging.INFO)
    result.propagate = False
    result.addHandler(_ConsoleHandler("stdout", logging.INFO, logging.INFO))
    result.addHandler(_ConsoleHandler("stderr", logging.WARNING))
    return result


class Console:
    """One output vocabulary for logs and structured command screens."""

    def __init__(self, name: str = "rootkitlings") -> None:
        self.log = _logger(name)

    @staticmethod
    def _stream(error: bool = False) -> TextIO:
        return sys.stderr if error else sys.stdout

    def line(
        self,
        text: str = "",
        *,
        style: str | None = None,
        error: bool = False,
    ) -> None:
        stream = self._stream(error)
        print(
            _styled(text, style, stream) if style else text,
            file=stream,
            flush=True,
        )

    def raw(self, text: str, *, error: bool = False) -> None:
        stream = self._stream(error)
        stream.write(text)
        stream.flush()

    def raw_block(self, text: str, *, error: bool = False) -> None:
        """Write captured command output and end before the next UI record."""
        self.raw(text, error=error)
        if text and not text.endswith("\n"):
            self.raw("\n", error=error)

    def heading(self, text: str, *, error: bool = False) -> None:
        self.line(text, style="accent", error=error)

    def section(self, text: str, *, error: bool = False) -> None:
        self.line(error=error)
        self.heading(text, error=error)

    def field(
        self,
        label: str,
        value: str,
        *,
        width: int = 11,
        value_style: str | None = None,
    ) -> None:
        stream = self._stream()
        shown_label = _styled(f"{label:<{width}}", "dim", stream)
        shown_value = (
            _styled(value, value_style, stream)
            if value_style
            else value
        )
        print(f"{shown_label}{shown_value}", file=stream, flush=True)

    def row(
        self,
        left: str,
        right: str,
        *,
        width: int = 22,
        left_style: str | None = None,
    ) -> None:
        stream = self._stream()
        column = f"{left:<{width}}"
        if left_style:
            column = _styled(column, left_style, stream)
        print(f"  {column}{right}", file=stream, flush=True)

    def bullet(self, text: str) -> None:
        self.line(f"- {text}")

    def command(self, value: str, *, error: bool = False) -> None:
        self.line(f"  {value}", style="command", error=error)

    def transcript(self, command: list[object] | tuple[object, ...]) -> None:
        shown = shlex.join(str(value) for value in command)
        self.line(f"$ {shown}", style="dim")

    def process_output(self, text: str, *, error: bool = False) -> None:
        if text:
            self.raw_block(text, error=error)
        else:
            self.line("(command produced no output)", style="dim", error=error)

    def process_result(
        self,
        returncode: int | None,
        *,
        expected_nonzero: bool = False,
    ) -> None:
        if returncode is None:
            self.field("Exit", "not available", value_style="error")
            return
        matched = returncode != 0 if expected_nonzero else returncode == 0
        value = str(returncode)
        if expected_nonzero:
            value += " (expected non-zero)"
        self.field(
            "Exit",
            value,
            value_style="success" if matched else "error",
        )

    def progress(self, message: str) -> None:
        self.log.info(message, extra={"console_kind": "progress"})

    def success(self, message: str) -> None:
        self.log.info(message, extra={"console_kind": "success"})

    def note(self, message: str) -> None:
        self.log.info(message)

    def warning(self, message: str) -> None:
        self.log.warning(message, extra={"console_kind": "warning"})

    def error(self, message: str) -> None:
        self.log.error(message, extra={"console_kind": "error"})

    @staticmethod
    def terminal_width(default: int = 80, maximum: int = 80) -> int:
        requested = os.environ.get("COLUMNS")
        if requested and requested.isdigit() and int(requested) > 0:
            return min(int(requested), maximum)
        return min(shutil.get_terminal_size((default, 24)).columns, maximum)

    @staticmethod
    def color_enabled(*, error: bool = False) -> bool:
        return _color_enabled(sys.stderr if error else sys.stdout)

    @staticmethod
    def style(text: str, style: str, *, error: bool = False) -> str:
        stream = sys.stderr if error else sys.stdout
        return _styled(text, style, stream)
