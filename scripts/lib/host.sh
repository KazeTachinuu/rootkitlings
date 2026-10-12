#!/usr/bin/env bash

package_name()
{
	# shellcheck disable=SC2154 # Set by the caller after distro detection.
	case "$family:$1" in
		arch:build|arch:compiler|arch:binutils) printf base-devel ;;
		debian:build|debian:compiler) printf build-essential ;;
		arch:python) printf python ;;
		arch:glibc|fedora:glibc) printf glibc ;;
		debian:glibc) printf libc-bin ;;
		arch:static-libc) printf glibc ;;
		debian:libelf) printf libelf-dev ;;
		debian:libcrypto) printf libssl-dev ;;
		debian:static-libc) printf libc6-dev ;;
		debian:compiler-rt) printf libclang-rt-dev ;;
		arch:script) printf util-linux ;;
		debian:script) printf bsdutils ;;
		fedora:script) printf util-linux-script ;;
		fedora:build) printf make ;;
		fedora:compiler) printf gcc ;;
		fedora:libelf) printf elfutils-libelf-devel ;;
		fedora:libcrypto) printf openssl-devel ;;
		fedora:static-libc) printf glibc-static ;;
		arch:compiler-rt|fedora:compiler-rt) printf compiler-rt ;;
		arch:libelf) printf libelf ;;
		arch:libcrypto) printf openssl ;;
		arch:clangd) printf clang ;;
		arch:clang|debian:clang|fedora:clang) printf clang ;;
		debian:clangd) printf clangd ;;
		fedora:clangd) printf clang-tools-extra ;;
		*:binutils) printf binutils ;;
		debian:busybox) printf busybox-static ;;
		fedora:busybox) printf busybox ;;
		fedora:qemu) printf qemu-system-x86-core ;;
		*:qemu) printf qemu-system-x86 ;;
		*:python) printf python3 ;;
		*) printf '%s' "$1" ;;
	esac
}

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
