// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/in.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#define RK_FRAME_MAX 64
#define RK_DRAIN_CHUNK 64

#define RK_FAIL_NONE 0
#define RK_FAIL_WORKER_CREATE 1
#define RK_FAIL_PROC_PUBLISH 2

struct socket;
struct task_struct;

struct rk_session_ops {
	__must_check int (*create)(struct socket **socket, void *context);
	__must_check int (*connect)(struct socket *socket,
				      const struct sockaddr_in *peer,
				      void *context);
	ssize_t (*send)(struct socket *socket, const unsigned char *bytes,
			size_t length, void *context);
	ssize_t (*receive)(struct socket *socket, unsigned char *bytes,
			   size_t length, void *context);
	int (*shutdown)(struct socket *socket, void *context);
	void (*release)(struct socket *socket, void *context);
	void (*wake)(void *context);
};

struct rk_frame {
	size_t length;
	unsigned char payload[RK_FRAME_MAX];
};

struct rk_session {
	struct mutex lock;
	wait_queue_head_t wait;
	struct task_struct *task;
	struct socket *active;
	const struct rk_session_ops *ops;
	void *ops_context;
	struct sockaddr_in peer;
	bool stopping;
	bool running;
	bool shutdown_started;
	bool initialized;
	u64 frames;
	u64 oversized;
	int last_error;
};

struct rk_session_status {
	bool running;
	bool active;
	bool stopping;
	u16 peer_port;
	u64 frames;
	u64 oversized;
	int last_error;
};

__must_check int rk_send_frame_ops(const struct rk_session_ops *ops,
				   void *context, struct socket *socket,
				   const unsigned char *payload, size_t length);
__must_check int rk_receive_frame_ops(const struct rk_session_ops *ops,
				      void *context, struct socket *socket,
				      struct rk_frame *frame);
__must_check int rk_session_start_with_ops(
	struct rk_session *session, u16 peer_port,
	const struct rk_session_ops *ops, void *context);
__must_check int rk_session_start(struct rk_session *session, u16 peer_port);
void rk_session_stop(struct rk_session *session);
void rk_session_snapshot(struct rk_session *session,
			 struct rk_session_status *status);

__must_check int rk_proc_publish(struct rk_session *session);
void rk_proc_unpublish(void);
__must_check int rk_run_selftests(void);

#endif
