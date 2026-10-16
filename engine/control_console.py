"""Engine control console: thin wrapper around the shared operator library."""

import lib.operator as _config
_config.state_dir = ".rklings"

from lib.operator.control_console import *  # noqa: E402, F401, F403
from lib.operator.control_console import (  # noqa: E402
    COMMAND_HELP,
    OpenSslAead,
    OperatorConsole,
    OperatorError,
    SecureControl,
    accept_shell,
    build_crypto_helper,
    encode_control_command,
    parse_shell_ticket,
    poll_shell_status,
    receive_frame,
    relay_shell,
    send_frame,
)
