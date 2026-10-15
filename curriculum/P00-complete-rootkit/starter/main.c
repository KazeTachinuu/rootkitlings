// SPDX-License-Identifier: GPL-2.0
//
// Final project: assemble and verify the complete rootkit.
//
// You have built every subsystem across 27 lessons and 6 milestones.
// Now write the complete module from scratch. Use the verification
// checklist in the README to validate each capability.
//
#include <crypto/hash.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/utsname.h>
#include <net/inet_sock.h>
#include <net/net_namespace.h>

/*
 * TODO: Write the complete rootkit.
 *
 * This is not a fill-in-the-blanks exercise. Write the entire module
 * from the constants and state declarations through init and exit.
 *
 * Required subsystems:
 *
 *   1. Constants: key length, port, buffer size, target limits
 *   2. State: key, fingerprint, target arrays (file, pid, content, port),
 *      module cloaking, persistence, auto-opsec tracking
 *   3. Key management: hex2bin, SHA-256 fingerprint, memzero_explicit
 *   4. Socket I/O: rk_send, rk_recv using kernel_sendmsg/kernel_recvmsg
 *   5. Target operations: add, remove, remove_at for strings and ports
 *   6. Auto-OPSEC: rk_auto_hide (PID + port on connect),
 *      rk_auto_unhide (reverse-order removal on disconnect)
 *   7. Command handlers:
 *      - PING -> PONG
 *      - INFO -> utsname fields
 *      - STATUS -> counts and flags
 *      - LIST FILE/PID/CONTENT/PORT -> enumeration
 *      - HIDE/UNHIDE FILE/PID/CONTENT/PORT -> target management
 *      - HIDE/UNHIDE MODULE -> list_del/list_add
 *      - UNHIDE ALL -> reset all counts
 *      - PERSIST/UNPERSIST -> modules-load.d via call_usermodehelper
 *      - ESCALATE -> prepare_kernel_cred + commit_creds
 *      - DOWNLOAD -> chunked kernel_read
 *   8. Dispatcher: string matching, argument extraction
 *   9. Agent thread: accept loop with auto-opsec calls
 *  10. Init: key parse, fingerprint, socket create/bind/listen,
 *      kthread_run, complete goto rollback chain
 *  11. Exit: restore module visibility, stop thread, release socket,
 *      zero key material
 *
 * Constraints:
 *   - No pr_info, no pr_err, no printk: leave no trace in dmesg
 *   - Zero key material on every error path and on exit
 *   - Disguise kthread name as "kworker/u8:3"
 *   - All responses through rk_send (encrypted in production)
 *
 * Verify with the checklist in the README.
 */

static int __init rk_init(void)
{
	/* TODO: implement */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: implement */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
