#!/bin/sh
# shellcheck disable=SC3043 # BusyBox ash provides local; checks run under ash.
# Assertion library sourced by exercise checks inside the BusyBox guest.

# shellcheck source=scripts/lib/ui.sh
. /vm/ui.sh

# -- marker output ----------------------------------------------------------

_pass=0
_fail=0

_ok() {
    _pass=$(( _pass + 1 ))
    ui_pass "$*"
}

_fail() {
    _fail=$(( _fail + 1 ))
    ui_fail "$*"
}

_hdr() {
    ui_heading "$*"
}

# -- lifecycle ---------------------------------------------------------------

begin() {
    _hdr "$1"
    dmesg -c > /dev/null 2>&1
}

# -- scoped allocation tracing ----------------------------------------------

kmem_trace_setup() {
    local format

    _kmem_trace_root=/sys/kernel/tracing
    _kmem_kmalloc_enable=$_kmem_trace_root/events/kmem/kmalloc/enable
    _kmem_kfree_enable=$_kmem_trace_root/events/kmem/kfree/enable
    _kmem_old_tracing=$(cat "$_kmem_trace_root/tracing_on")
    _kmem_old_kmalloc=$(cat "$_kmem_kmalloc_enable")
    _kmem_old_kfree=$(cat "$_kmem_kfree_enable")
    _kmem_trace_copy=/tmp/rootkitlings-kmem-trace
    _kmem_allocs=/tmp/rootkitlings-kmem-allocs
    _kmem_frees=/tmp/rootkitlings-kmem-frees

    for format in \
        "$_kmem_trace_root/events/kmem/kmalloc/format" \
        "$_kmem_trace_root/events/kmem/kfree/format"; do
        if grep -q 'field:.* call_site;' "$format" &&
           grep -q 'field:.* ptr;' "$format"; then
            _ok "$(basename "$(dirname "$format")") trace fields are available"
        else
            _fail "$format lacks call_site or ptr"
        fi
    done
}

kmem_trace_disable() {
    printf 0 > "$_kmem_trace_root/tracing_on"
    printf 0 > "$_kmem_kmalloc_enable"
    printf 0 > "$_kmem_kfree_enable"
}

kmem_trace_start() {
    kmem_trace_disable
    : > "$_kmem_trace_root/trace"
    printf 1 > "$_kmem_kmalloc_enable"
    printf 1 > "$_kmem_kfree_enable"
    printf 1 > "$_kmem_trace_root/tracing_on"
}

kmem_trace_stop() {
    kmem_trace_disable
    cat "$_kmem_trace_root/trace" > "$_kmem_trace_copy"
}

assert_one_loader_allocation_freed() {
    local label line kind field value call_site ptr matched alloc_count free_count
    local alloc_ptr free_ptr

    label=$1
    : > "$_kmem_allocs"
    : > "$_kmem_frees"
    while IFS= read -r line; do
        case "$line" in
            *module_load_tes-*' kmalloc: '*) kind=alloc ;;
            *module_load_tes-*' kfree: '*) kind=free ;;
            *) continue ;;
        esac
        call_site=
        ptr=
        for field in $line; do
            case "$field" in
                call_site=*)
                    value=${field#call_site=}
                    case "$value" in
                        0x*) call_site=$value ;;
                        ''|*[!0-9a-fA-F]*) ;;
                        *) call_site=0x$value ;;
                    esac
                    ;;
                ptr=*)
                    value=${field#ptr=}
                    case "$value" in
                        0x*) ptr=${value#0x} ;;
                        ''|*[!0-9a-fA-F]*) ;;
                        *) ptr=$value ;;
                    esac
                    ;;
            esac
        done
        [ -n "$call_site" ] && [ -n "$ptr" ] || continue
        case "$kind" in
            alloc) printf '%s %s\n' "$ptr" "$call_site" >> "$_kmem_allocs" ;;
            free) printf '%s %s\n' "$ptr" "$call_site" >> "$_kmem_frees" ;;
        esac
    done < "$_kmem_trace_copy"

    matched=
    alloc_count=$(wc -l < "$_kmem_allocs")
    free_count=$(wc -l < "$_kmem_frees")
    if [ "$alloc_count" -eq 1 ] && [ "$free_count" -eq 1 ]; then
        read -r alloc_ptr _ < "$_kmem_allocs"
        read -r free_ptr _ < "$_kmem_frees"
        if [ "$alloc_ptr" = "$free_ptr" ] &&
           [ "$alloc_ptr" != 0000000000000000 ]; then
            matched=$alloc_ptr
        fi
    fi

    if [ -n "$matched" ]; then
        _ok "$label"
    else
        _fail "$label: expected one matching non-null allocation and free"
        printf '    parsed allocations:\n'
        head -n 12 "$_kmem_allocs"
        printf '    parsed frees:\n'
        head -n 12 "$_kmem_frees"
        printf '    raw trace:\n'
        head -n 30 "$_kmem_trace_copy"
    fi
}

kmem_trace_restore() {
    kmem_trace_disable
    : > "$_kmem_trace_root/trace"
    printf '%s' "$_kmem_old_kmalloc" > "$_kmem_kmalloc_enable"
    printf '%s' "$_kmem_old_kfree" > "$_kmem_kfree_enable"
    printf '%s' "$_kmem_old_tracing" > "$_kmem_trace_root/tracing_on"
    rm -f "$_kmem_trace_copy" "$_kmem_allocs" "$_kmem_frees"
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
    ui_field Results "${_pass} passed, ${_fail} failed"
    if [ "$_fail" -gt 0 ]; then
        exit 1
    fi
    exit 0
}

# -- module operations -------------------------------------------------------

_shell_quote() {
    local value

    case $1 in
        '') printf "''" ;;
        *[!A-Za-z0-9_./:=,@%+-]*)
            value=$(printf '%s' "$1" | sed "s/'/'\"'\"'/g")
            printf "'%s'" "$value"
            ;;
        *) printf '%s' "$1" ;;
    esac
}

_show_command() {
    local argument command quoted

    command=
    for argument in "$@"; do
        quoted=$(_shell_quote "$argument")
        command="${command}${command:+ }$quoted"
    done
    ui_transcript "$command"
}

load() {
    local mod="$1"; shift
    _show_command insmod "/rootkitlings/$mod" "$@"
    if insmod "/rootkitlings/$mod" "$@"; then
        _ok "loaded $mod"
    else
        _fail "failed to load $mod"
    fi
}

unload() {
    local mod="$1"
    _show_command rmmod "$mod"
    if rmmod "$mod" && [ ! -d "/sys/module/$mod" ] &&
       ! grep -q "^$mod " /proc/modules; then
        _ok "unloaded $mod"
    else
        _fail "failed to unload $mod"
    fi
}

load_fail() {
    local mod="$1"; shift
    _show_command insmod "/rootkitlings/$mod" "$@"
    if insmod "/rootkitlings/$mod" "$@"; then
        _fail "expected $mod to be rejected, but it loaded"
    else
        _ok "$mod correctly rejected"
    fi
}

load_errno() {
    local module="$1" expected="$2" parameters="${3-}"

    _show_command module_load_params "$module" "$expected" "$parameters"
    module_load_params "$module" "$expected" "$parameters"
}

write_param() {
    local mod="$1" param="$2" value="$3"
    local path="/sys/module/$mod/parameters/$param" quoted_path quoted_value

    quoted_path=$(_shell_quote "$path")
    quoted_value=$(_shell_quote "$value")
    ui_transcript "printf %s $quoted_value > $quoted_path"
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
