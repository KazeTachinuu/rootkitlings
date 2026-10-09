#!/usr/bin/env bash

host_kvm_available()
{
	local qemu="${1:-qemu-system-x86_64}"
	local device="${2:-/dev/kvm}"
	local probe pid status=1

	[ -r "$device" ] && [ -w "$device" ] && command -v "$qemu" >/dev/null || return 1
	probe=$(mktemp -d "${TMPDIR:-/tmp}/rootkitlings-kvm.XXXXXX") || return 1
	if "$qemu" -accel kvm -machine none -nodefaults -display none -S \
		-daemonize -pidfile "$probe/pid" >/dev/null 2>&1 &&
		read -r pid < "$probe/pid" && kill -0 "$pid" 2>/dev/null; then
		status=0
		kill "$pid" 2>/dev/null || true
	fi
	rm -r -- "$probe"
	return "$status"
}
