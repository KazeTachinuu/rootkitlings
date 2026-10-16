"""Operational server: thin wrapper around the shared operator library."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

import lib.operator as _config
_config.state_dir = ".state"

from lib.operator.server import (  # noqa: E402
    Target,
    TargetManager,
    ServerConsole,
    read_server_state,
    run_server as _run_server,
)
from lib.operator.control_console import OperatorError  # noqa: E402


def run_server(
    root: Path,
    *,
    transcript: Callable[[list[object]], None],
    **kwargs,
) -> int:
    return _run_server(
        root,
        transcript=transcript,
        banner=(
            "Rootkitlings operator server\n"
            "Watching for targets. Start one with:\n"
            "  make vm\n\n"
        ),
        webapp_dist=root / "webapp" / "dist",
        **kwargs,
    )
