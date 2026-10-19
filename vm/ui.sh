#!/bin/sh
# Colorized output helpers for the BusyBox guest.
# Sourced by vm/checks/lib.sh and vm/init.

if [ -n "${NO_COLOR:-}" ]; then
    _green=''; _red=''; _cyan=''; _yellow=''; _blue=''; _dim=''; _bold=''; _reset=''
else
    _green='\033[32m'  _red='\033[31m'    _cyan='\033[36m'
    _yellow='\033[33m' _blue='\033[34m'   _dim='\033[2m'
    _bold='\033[1m'    _reset='\033[0m'
fi

ui_pass()       { printf "${_green}[+] %s${_reset}\n" "$*"; }
ui_fail()       { printf "${_red}[-] %s${_reset}\n" "$*"; }
ui_heading()    { printf "${_cyan}[*] %s${_reset}\n" "$*"; }
ui_success()    { printf "${_green}[+] %s${_reset}\n" "$*"; }
ui_error()      { printf "${_red}[!] %s${_reset}\n" "$*"; }
ui_warning()    { printf "${_yellow}[!] %s${_reset}\n" "$*"; }
ui_progress()   { printf "${_blue}[*] %s${_reset}\n" "$*"; }
ui_transcript() { printf "${_dim}    %s${_reset}\n" "$*"; }
ui_field()      { printf "${_bold}%-16s${_reset} %s\n" "$1" "$2"; }
ui_die()        { printf "${_red}[!] %s${_reset}\n" "$*" >&2; exit 1; }
