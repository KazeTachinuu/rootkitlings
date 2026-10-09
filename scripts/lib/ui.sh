#!/bin/sh
# Shared terminal output for Rootkitlings shell commands.
# shellcheck disable=SC3043 # BusyBox ash and Bash provide local variables.

_ui_color_enabled()
{
	[ -t "$1" ] && [ "${NO_COLOR+x}" != x ] && [ "${TERM:-}" != dumb ]
}

_ui_emit()
{
	local fd="$1" code="$2" label="$3"
	shift 3
	if _ui_color_enabled "$fd"; then
		if [ "$fd" -eq 2 ]; then
			printf '\033[%sm%s\033[0m%s%s\n' "$code" "$label" \
				"${label:+ }" "$*" >&2
		else
			printf '\033[%sm%s\033[0m%s%s\n' "$code" "$label" \
				"${label:+ }" "$*"
		fi
	elif [ "$fd" -eq 2 ]; then
		printf '%s%s%s\n' "$label" "${label:+ }" "$*" >&2
	else
		printf '%s%s%s\n' "$label" "${label:+ }" "$*"
	fi
}

ui_heading()  { [ "${ROOTKITLINGS_QUIET:-0}" = 1 ] || _ui_emit 1 '1;34' '[*]' "$*"; }
ui_progress() { [ "${ROOTKITLINGS_QUIET:-0}" = 1 ] || _ui_emit 1 '1;34' '[*]' "$*"; }
ui_success()  { [ "${ROOTKITLINGS_QUIET:-0}" = 1 ] || _ui_emit 1 '1;32' '[+]' "$*"; }
ui_pass()     { _ui_emit 1 '1;32' '[+]' "$*"; }
ui_fail()     { _ui_emit 1 '1;31' '[x]' "$*"; }
ui_warning()  { _ui_emit 2 '1;33' '[!]' "$*"; }
ui_error()    { _ui_emit 2 '1;31' '[x]' "$*"; }
ui_die()      { ui_error "$*"; exit 1; }

ui_field()
{
	local label="$1"
	shift
	printf '%-10s%s\n' "$label" "$*"
}

ui_item()
{
	local state="$1" label="$2" message
	shift 2
	message=$(printf '%-22s%s' "$label" "$*")
	case $state in
		pass) ui_pass "$message" ;;
		fail) ui_fail "$message" ;;
		note) _ui_emit 1 '2' '[-]' "$message" ;;
		*) ui_die "unknown UI item state: $state" ;;
	esac
}

ui_command()
{
	if _ui_color_enabled 1; then
		printf '  \033[1;36m%s\033[0m\n' "$*"
	else
		printf '  %s\n' "$*"
	fi
}

ui_transcript()
{
	if _ui_color_enabled 1; then
		printf '    \033[2m$ %s\033[0m\n' "$*"
	else
		printf '    $ %s\n' "$*"
	fi
}
