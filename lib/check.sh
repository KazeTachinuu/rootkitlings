#!/bin/sh
# shellcheck disable=SC3043 # BusyBox ash provides local; checks run under ash.
# check.sh -- POSIX sh assertion library for rootkitlings
# Sourced by per-ling check scripts inside the busybox QEMU VM.

# -- marker output ----------------------------------------------------------

_pass=0
_fail=0

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ] && [ "${TERM:-}" != dumb ]; then
    B='\033[1;34m' G='\033[1;32m' R='\033[31m' N='\033[0m'
else
    B='' G='' R='' N=''
fi

_ok() {
    _pass=$(( _pass + 1 ))
    printf "${G}[+]${N} %s\n" "$*"
}

_fail() {
    _fail=$(( _fail + 1 ))
    printf "${R}[x]${N} %s\n" "$*"
}

_hdr() {
    printf "${B}[*]${N} %s\n" "$*"
}

# -- lifecycle ---------------------------------------------------------------

begin() {
    _hdr "$1"
    dmesg -c > /dev/null 2>&1
}

summary() {
    local log

    log=$(dmesg)
    case "$log" in
        *"BUG: kernel"*|*"WARNING: CPU:"*|*"Oops:"*|*"KASAN:"*|*"UBSAN:"*|\
        *"general protection fault"*|*"kernel NULL pointer dereference"*)
            _fail "kernel reported a bug or warning"
            ;;
        *)
            _ok "kernel log is clean"
            ;;
    esac

    printf "\n"
    _hdr "results: ${_pass} passed, ${_fail} failed"
    if [ "$_fail" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

# -- module operations -------------------------------------------------------

load() {
    local mod="$1"; shift
    if insmod "/rootkitlings/$mod" "$@"; then
        _ok "loaded $mod"
    else
        _fail "failed to load $mod"
    fi
}

unload() {
    local mod="$1"
    if rmmod "$mod"; then
        _ok "unloaded $mod"
    else
        _fail "failed to unload $mod"
    fi
}

load_fail() {
    local mod="$1"; shift
    if insmod "/rootkitlings/$mod" "$@" 2>/dev/null; then
        _fail "expected $mod to be rejected, but it loaded"
    else
        _ok "$mod correctly rejected"
    fi
}

write_param() {
    local mod="$1" param="$2" value="$3"
    printf '%s' "$value" > "/sys/module/$mod/parameters/$param"
}

# -- assertions --------------------------------------------------------------

assert_loaded() {
    local mod="$1"
    if test -d "/sys/module/$mod"; then
        _ok "$mod is loaded"
    else
        _fail "$mod is not loaded"
    fi
}

assert_absent() {
    local path="$1"
    if test ! -e "$path"; then
        _ok "$path absent"
    else
        _fail "$path exists (expected absent)"
    fi
}

assert_exists() {
    local path="$1"
    if test -e "$path"; then
        _ok "$path exists"
    else
        _fail "$path does not exist (expected present)"
    fi
}

assert_param() {
    local mod="$1" param="$2" expected="$3"
    local actual
    actual=$(cat "/sys/module/$mod/parameters/$param")
    if [ "$actual" = "$expected" ]; then
        _ok "$mod.$param == $expected"
    else
        _fail "$mod.$param: expected '$expected', got '$actual'"
    fi
}

assert_read() {
    local path="$1" expected="$2"
    local actual
    actual=$(cat "$path")
    if [ "$actual" = "$expected" ]; then
        _ok "$path == $expected"
    else
        _fail "$path: expected '$expected', got '$actual'"
    fi
}

assert_log() {
    local msg="$1"
    local log
    log=$(dmesg)
    case "$log" in
        *"$msg"*)
            _ok "dmesg contains: $msg"
            ;;
        *)
            _fail "dmesg missing: $msg"
            ;;
    esac
}

assert_gt() {
    local path="$1" threshold="$2"
    local actual
    actual=$(cat "$path")
    case "$actual" in
        ''|*[!0-9]*) _fail "$path: expected an unsigned integer, got '$actual'" ;;
        *)
            if [ "$actual" -gt "$threshold" ]; then
                _ok "$path ($actual) > $threshold"
            else
                _fail "$path: expected > $threshold, got $actual"
            fi
            ;;
    esac
}

assert_contains() {
    local path="$1" expected="$2"
    local actual
    actual=$(cat "$path")
    case "$actual" in
        *"$expected"*)
            _ok "$path contains: $expected"
            ;;
        *)
            _fail "$path: missing '$expected', got '$actual'"
            ;;
    esac
}
