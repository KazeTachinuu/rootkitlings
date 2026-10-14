"""Engine control console: thin wrapper around the shared operator library."""

import lib.operator as _config
_config.state_dir = ".rklings"

from lib.operator.control_console import *  # noqa: E402, F401, F403
from lib.operator.control_console import (  # noqa: E402
    COMMAND_HELP,
    HostOperator,
    OpenSslAead,
    OperatorConsole,
    OperatorError,
    SecureControl,
    TargetProcess,
    TargetSession,
    accept_shell_while_running,
    build_crypto_helper,
    create_target_session,
    encode_control_command,
    hold_ports,
    mark_operator_ready,
    parse_shell_ticket,
    poll_shell_status,
    read_target_session,
    receive_frame,
    relay_shell,
    release_ports,
    run_control_console,
    send_frame,
    wait_for_operator,
)
