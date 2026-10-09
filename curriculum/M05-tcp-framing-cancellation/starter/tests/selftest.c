// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "rootkit.h"

enum rk_fake_block {
	RK_BLOCK_NONE,
	RK_BLOCK_CREATE,
	RK_BLOCK_CONNECT,
	RK_BLOCK_HEADER,
	RK_BLOCK_PAYLOAD,
	RK_BLOCK_SEND,
	RK_BLOCK_DRAIN,
};

struct rk_fake {
	wait_queue_head_t wait;
	atomic_t phase;
	atomic_t awakened;
	atomic_t create_calls;
	atomic_t connect_calls;
	atomic_t send_calls;
	atomic_t receive_calls;
	atomic_t shutdowns;
	atomic_t releases;
	enum rk_fake_block block;
	const unsigned char *input;
	size_t input_length;
	size_t input_offset;
	unsigned char *output;
	size_t output_capacity;
	size_t output_length;
	size_t send_limit;
	size_t receive_limit;
	int send_fail_call;
	ssize_t send_fail_result;
	int receive_fail_call;
	ssize_t receive_fail_result;
	int create_result;
	int connect_result;
	bool null_socket;
};

static unsigned int assertions;

static int rk_expect(bool condition, const char *label)
{
	assertions++;
	if (condition)
		return 0;
	pr_err("rootkitlings: session self-test failed: %s\n", label);
	return -EINVAL;
}

static void rk_fake_init(struct rk_fake *fake)
{
	memset(fake, 0, sizeof(*fake));
	init_waitqueue_head(&fake->wait);
	atomic_set(&fake->phase, RK_BLOCK_NONE);
	atomic_set(&fake->awakened, 0);
	atomic_set(&fake->create_calls, 0);
	atomic_set(&fake->connect_calls, 0);
	atomic_set(&fake->send_calls, 0);
	atomic_set(&fake->receive_calls, 0);
	atomic_set(&fake->shutdowns, 0);
	atomic_set(&fake->releases, 0);
}

static void rk_fake_wait(struct rk_fake *fake, enum rk_fake_block phase)
{
	atomic_set(&fake->phase, phase);
	wake_up_all(&fake->wait);
	(void)wait_event_interruptible(fake->wait,
				       atomic_read(&fake->awakened));
}

static int rk_fake_create(struct socket **socket, void *context)
{
	struct rk_fake *fake = context;

	atomic_inc(&fake->create_calls);
	if (fake->create_result) {
		*socket = NULL;
		return fake->create_result;
	}
	*socket = fake->null_socket ? NULL : (struct socket *)fake;
	if (fake->block == RK_BLOCK_CREATE)
		rk_fake_wait(fake, RK_BLOCK_CREATE);
	return 0;
}

static int rk_fake_connect(struct socket *socket,
			   const struct sockaddr_in *peer, void *context)
{
	struct rk_fake *fake = context;

	(void)socket;
	(void)peer;
	atomic_inc(&fake->connect_calls);
	if (fake->block == RK_BLOCK_CONNECT) {
		rk_fake_wait(fake, RK_BLOCK_CONNECT);
		return -ECANCELED;
	}
	return fake->connect_result;
}

static ssize_t rk_fake_send(struct socket *socket,
			    const unsigned char *bytes, size_t length,
			    void *context)
{
	struct rk_fake *fake = context;
	int call = atomic_inc_return(&fake->send_calls);
	size_t count = length;

	(void)socket;
	if (fake->block == RK_BLOCK_SEND && call == 1) {
		rk_fake_wait(fake, RK_BLOCK_SEND);
		return -ECANCELED;
	}
	if (call == fake->send_fail_call)
		return fake->send_fail_result;
	if (fake->send_limit && count > fake->send_limit)
		count = fake->send_limit;
	if (count > fake->output_capacity - fake->output_length)
		return -ENOSPC;
	memcpy(fake->output + fake->output_length, bytes, count);
	fake->output_length += count;
	return count;
}

static ssize_t rk_fake_receive(struct socket *socket, unsigned char *bytes,
			       size_t length, void *context)
{
	struct rk_fake *fake = context;
	int call = atomic_inc_return(&fake->receive_calls);
	size_t count = length;

	(void)socket;
	if ((fake->block == RK_BLOCK_HEADER && call == 1) ||
	    (fake->block == RK_BLOCK_PAYLOAD && call == 2) ||
	    (fake->block == RK_BLOCK_DRAIN && call == 2)) {
		rk_fake_wait(fake, fake->block);
		return -ECANCELED;
	}
	if (call == fake->receive_fail_call)
		return fake->receive_fail_result;
	if (fake->input_offset == fake->input_length)
		return 0;
	if (fake->receive_limit && count > fake->receive_limit)
		count = fake->receive_limit;
	if (count > fake->input_length - fake->input_offset)
		count = fake->input_length - fake->input_offset;
	memcpy(bytes, fake->input + fake->input_offset, count);
	fake->input_offset += count;
	return count;
}

static int rk_fake_shutdown(struct socket *socket, void *context)
{
	struct rk_fake *fake = context;

	(void)socket;
	atomic_inc(&fake->shutdowns);
	atomic_set(&fake->awakened, 1);
	wake_up_all(&fake->wait);
	return 0;
}

static void rk_fake_release(struct socket *socket, void *context)
{
	struct rk_fake *fake = context;

	(void)socket;
	atomic_inc(&fake->releases);
}

static void rk_fake_wake(void *context)
{
	struct rk_fake *fake = context;

	atomic_set(&fake->awakened, 1);
	wake_up_all(&fake->wait);
}

static const struct rk_session_ops rk_fake_ops = {
	.transport = {
		.send = rk_fake_send,
		.receive = rk_fake_receive,
	},
	.create = rk_fake_create,
	.connect = rk_fake_connect,
	.shutdown = rk_fake_shutdown,
	.release = rk_fake_release,
	.wake = rk_fake_wake,
};

static void rk_make_payload(unsigned char *payload, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		payload[index] = (unsigned char)(index * 37U + length * 11U);
}

static int rk_test_positive_partials(void)
{
	unsigned char payload[RK_FRAME_MAX];
	unsigned char stream[RK_FRAME_MAX + 2];
	struct rk_frame frame;
	struct rk_fake fake;
	size_t length;
	size_t limit;

	for (length = 0; length <= RK_FRAME_MAX; length++) {
		rk_make_payload(payload, length);
		stream[0] = length >> 8;
		stream[1] = length;
		memcpy(stream + 2, payload, length);
		for (limit = 1; limit <= RK_FRAME_MAX + 2; limit++) {
			unsigned char output[RK_FRAME_MAX + 2];
			int error;

			rk_fake_init(&fake);
			fake.output = output;
			fake.output_capacity = sizeof(output);
			fake.send_limit = limit;
			error = rk_send_frame(&rk_fake_ops.transport, &fake,
						  (struct socket *)&fake,
						  payload, length);
			if (rk_expect(!error && fake.output_length == length + 2 &&
				      !memcmp(output, stream, length + 2),
				      "every positive partial send boundary"))
				return -EINVAL;

			rk_fake_init(&fake);
			fake.input = stream;
			fake.input_length = length + 2;
			fake.receive_limit = limit;
			memset(&frame, 0xa5, sizeof(frame));
			error = rk_receive_frame(&rk_fake_ops.transport, &fake,
						     (struct socket *)&fake,
						     &frame);
			if (rk_expect(!error && frame.length == length &&
				      !memcmp(frame.payload, payload, length) &&
				      fake.input_offset == length + 2,
				      "every positive partial receive boundary"))
				return -EINVAL;
		}
	}
	return 0;
}

static int rk_expect_send_failure(int call, ssize_t result, size_t limit,
				  int expected, const char *label)
{
	unsigned char payload[4] = { 0x00, 0xff, 0x41, 0x80 };
	unsigned char output[6];
	struct rk_fake fake;
	int error;

	rk_fake_init(&fake);
	fake.output = output;
	fake.output_capacity = sizeof(output);
	fake.send_limit = limit;
	fake.send_fail_call = call;
	fake.send_fail_result = result;
	error = rk_send_frame(&rk_fake_ops.transport, &fake,
				  (struct socket *)&fake, payload, sizeof(payload));
	return rk_expect(error == expected, label);
}

static int rk_expect_receive_failure(const unsigned char *input,
				     size_t input_length, size_t limit,
				     int call, ssize_t result, int expected,
				     const char *label)
{
	struct rk_frame before;
	struct rk_frame frame;
	struct rk_fake fake;
	int error;

	memset(&before, 0xa5, sizeof(before));
	frame = before;
	rk_fake_init(&fake);
	fake.input = input;
	fake.input_length = input_length;
	fake.receive_limit = limit;
	fake.receive_fail_call = call;
	fake.receive_fail_result = result;
	error = rk_receive_frame(&rk_fake_ops.transport, &fake,
				     (struct socket *)&fake, &frame);
	return rk_expect(error == expected &&
			 !memcmp(&frame, &before, sizeof(frame)), label);
}

static int rk_test_zero_negative_and_impossible(void)
{
	unsigned char valid[] = { 0, 4, 0x00, 0xff, 0x41, 0x80 };
	unsigned char oversized[67] = { 0, 65 };
	int call;

	for (call = 1; call <= 2; call++) {
		if (rk_expect_send_failure(call, 0, 1, -EPIPE,
					   "zero while sending a split header") ||
		    rk_expect_send_failure(call, -EAGAIN, 1, -EAGAIN,
					   "negative split-header send is preserved") ||
		    rk_expect_receive_failure(valid, sizeof(valid), 1, call, 0,
					      -ECONNRESET,
					      "zero while receiving a split header") ||
		    rk_expect_receive_failure(valid, sizeof(valid), 1, call,
					      -EAGAIN, -EAGAIN,
					      "negative split-header receive is preserved"))
			return -EINVAL;
	}
	for (call = 3; call <= 6; call++) {
		if (rk_expect_send_failure(call, 0, 1, -EPIPE,
					   "zero while sending a split payload") ||
		    rk_expect_send_failure(call, -ENOSPC, 1, -ENOSPC,
					   "negative split-payload send is preserved") ||
		    rk_expect_receive_failure(valid, sizeof(valid), 1, call, 0,
					      -ECONNRESET,
					      "zero while receiving a split payload") ||
		    rk_expect_receive_failure(valid, sizeof(valid), 1, call,
					      -ENOBUFS, -ENOBUFS,
					      "negative split-payload receive is preserved"))
			return -EINVAL;
	}
	for (call = 2; call <= 3; call++) {
		if (rk_expect_receive_failure(oversized, sizeof(oversized), 64,
					      call, 0, -ECONNRESET,
					      "zero while draining an oversized frame") ||
		    rk_expect_receive_failure(oversized, sizeof(oversized), 64,
					      call, -EIO, -EIO,
					      "negative oversized drain error is preserved"))
			return -EINVAL;
	}
	if (rk_expect_send_failure(1, 3, 64, -EPROTO,
				   "oversize positive header send is rejected") ||
	    rk_expect_send_failure(2, 5, 64, -EPROTO,
				   "oversize positive payload send is rejected") ||
	    rk_expect_receive_failure(valid, sizeof(valid), 64, 1, 3,
				      -EPROTO,
				      "oversize positive header receive is rejected") ||
	    rk_expect_receive_failure(valid, sizeof(valid), 64, 2, 5,
				      -EPROTO,
				      "oversize positive payload receive is rejected") ||
	    rk_expect_receive_failure(oversized, sizeof(oversized), 64, 2,
				      65, -EPROTO,
				      "oversize positive drain receive is rejected"))
		return -EINVAL;
	return 0;
}

static int rk_test_argument_validation(void)
{
	static const struct rk_session_ops no_send = {
		.transport = {
			.receive = rk_fake_receive,
		},
		.create = rk_fake_create,
		.connect = rk_fake_connect,
		.shutdown = rk_fake_shutdown,
		.release = rk_fake_release,
		.wake = rk_fake_wake,
	};
	unsigned char output[2];
	struct rk_session_status status;
	struct rk_session session = { };
	struct rk_frame frame;
	struct rk_fake fake;

	rk_fake_init(&fake);
	fake.output = output;
	fake.output_capacity = sizeof(output);
	memset(&status, 0xa5, sizeof(status));
	rk_session_snapshot(NULL, &status);
	if (rk_expect(!memcmp(&status, &(struct rk_session_status) { },
			     sizeof(status)),
		      "snapshot clears output for an absent session") ||
	    rk_expect(rk_send_frame(NULL, &fake,
				       (struct socket *)&fake, NULL, 0) == -EINVAL,
		      "send rejects an absent operation table") ||
	    rk_expect(rk_send_frame(&rk_fake_ops.transport, &fake, NULL,
				       NULL, 0) == -EINVAL,
		      "send rejects an absent socket") ||
	    rk_expect(rk_send_frame(&rk_fake_ops.transport, &fake,
				       (struct socket *)&fake, NULL, 1) == -EINVAL,
		      "send rejects an absent non-empty payload") ||
	    rk_expect(rk_send_frame(&rk_fake_ops.transport, &fake,
				       (struct socket *)&fake, output,
				       RK_FRAME_MAX + 1) == -EINVAL,
		      "send rejects a frame above the wire limit") ||
	    rk_expect(rk_receive_frame(NULL, &fake,
					  (struct socket *)&fake,
					  &frame) == -EINVAL,
		      "receive rejects an absent operation table") ||
	    rk_expect(rk_receive_frame(&rk_fake_ops.transport, &fake, NULL,
					  &frame) == -EINVAL,
		      "receive rejects an absent socket") ||
	    rk_expect(rk_receive_frame(&rk_fake_ops.transport, &fake,
					  (struct socket *)&fake,
					  NULL) == -EINVAL,
		      "receive rejects an absent output frame") ||
	    rk_expect(rk_session_start_with_ops(NULL, 47023, &rk_fake_ops,
					       &fake) == -EINVAL,
		      "start rejects an absent session") ||
	    rk_expect(rk_session_start_with_ops(&session, 0, &rk_fake_ops,
					       &fake) == -EINVAL,
		      "start rejects port zero") ||
	    rk_expect(rk_session_start_with_ops(&session, 47023, &no_send,
					       &fake) == -EINVAL,
		      "start rejects an incomplete operation table"))
		return -EINVAL;
	return 0;
}

static int rk_test_oversized(size_t oversized_length)
{
	const size_t valid_length = 3;
	const size_t total = 2 + oversized_length + 2 + valid_length;
	unsigned char *input;
	struct rk_frame before;
	struct rk_frame frame;
	struct rk_fake fake;
	int error = -EINVAL;

	input = kmalloc(total, GFP_KERNEL);
	if (!input)
		return -ENOMEM;
	input[0] = oversized_length >> 8;
	input[1] = oversized_length;
	memset(input + 2, 0x5a, oversized_length);
	input[2 + oversized_length] = 0;
	input[3 + oversized_length] = valid_length;
	input[4 + oversized_length] = 0;
	input[5 + oversized_length] = 0xff;
	input[6 + oversized_length] = 0x7a;

	rk_fake_init(&fake);
	fake.input = input;
	fake.input_length = total;
	fake.receive_limit = oversized_length == 65 ? 1 : RK_DRAIN_CHUNK;
	memset(&before, 0xa5, sizeof(before));
	frame = before;
	if (rk_expect(rk_receive_frame(&rk_fake_ops.transport, &fake,
						   (struct socket *)&fake,
						   &frame) == -EMSGSIZE &&
		      fake.input_offset == 2 + oversized_length &&
		      !memcmp(&frame, &before, sizeof(frame)),
		      "oversized frame is fully drained without output") ||
	    rk_expect(!rk_receive_frame(&rk_fake_ops.transport, &fake,
					    (struct socket *)&fake, &frame) &&
		      frame.length == valid_length && frame.payload[0] == 0 &&
		      frame.payload[1] == 0xff && frame.payload[2] == 0x7a &&
		      fake.input_offset == total,
		      "valid frame after oversized input remains aligned"))
		goto out;
	error = 0;
out:
	kfree(input);
	return error;
}

static int rk_test_blocked_stop(enum rk_fake_block block)
{
	unsigned char input[67] = { 0, 65 };
	struct rk_session_status status;
	struct rk_session session = { };
	struct rk_fake fake;
	long reached;
	int error;

	if (block == RK_BLOCK_PAYLOAD) {
		input[0] = 0;
		input[1] = 1;
		input[2] = 0x42;
	} else if (block == RK_BLOCK_SEND) {
		input[0] = 0;
		input[1] = 0;
	}
	rk_fake_init(&fake);
	fake.block = block;
	fake.input = input;
	fake.input_length = sizeof(input);
	fake.receive_limit = RK_DRAIN_CHUNK;
	error = rk_session_start_with_ops(&session, 47023, &rk_fake_ops,
					  &fake);
	if (rk_expect(!error, "instrumented session starts"))
		return -EINVAL;
	reached = wait_event_timeout(fake.wait,
				     atomic_read(&fake.phase) == block, HZ);
	if (rk_expect(reached > 0, "worker reaches the selected blocked phase")) {
		rk_session_stop(&session);
		return -EINVAL;
	}
	if (block == RK_BLOCK_CREATE &&
	    rk_expect(rk_session_start_with_ops(&session, 47023,
						&rk_fake_ops, &fake) == -EBUSY,
		      "repeat start cannot replace an owned worker")) {
		rk_session_stop(&session);
		return -EINVAL;
	}
	rk_session_stop(&session);
	memset(&status, 0, sizeof(status));
	rk_session_snapshot(&session, &status);
	if (rk_expect(!status.running && !status.active && status.stopping,
		      "stop joins and clears the published socket") ||
	    rk_expect(status.last_error == 0,
		      "stop-induced transport errors are not session failures") ||
	    rk_expect(atomic_read(&fake.releases) == 1,
		      "the worker releases its socket exactly once") ||
	    rk_expect(atomic_read(&fake.shutdowns) ==
		      (block == RK_BLOCK_CREATE ? 0 : 1),
		      "stop shuts down exactly the socket it observed") ||
	    rk_expect(block != RK_BLOCK_CREATE ||
		      atomic_read(&fake.connect_calls) == 0,
		      "stop gate prevents late publication after create"))
		return -EINVAL;
	rk_session_stop(&session);
	if (rk_expect(atomic_read(&fake.releases) == 1 &&
		      atomic_read(&fake.shutdowns) ==
		      (block == RK_BLOCK_CREATE ? 0 : 1),
		      "repeated stop does not reacquire socket ownership"))
		return -EINVAL;
	return 0;
}

static int rk_test_worker_failures(void)
{
	struct rk_session_status status;
	struct rk_session session = { };
	struct rk_fake fake;
	long finished;

	rk_fake_init(&fake);
	fake.create_result = -ENOBUFS;
	if (rk_expect(!rk_session_start_with_ops(&session, 47023,
						&rk_fake_ops, &fake),
		      "worker starts before an injected create error"))
		return -EINVAL;
	finished = wait_event_timeout(session.wait,
				      !READ_ONCE(session.running), HZ);
	if (rk_expect(finished > 0, "create-error worker terminates")) {
		rk_session_stop(&session);
		return -EINVAL;
	}
	rk_session_stop(&session);
	memset(&status, 0, sizeof(status));
	rk_session_snapshot(&session, &status);
	if (rk_expect(status.last_error == -ENOBUFS &&
		      atomic_read(&fake.create_calls) == 1 &&
		      atomic_read(&fake.connect_calls) == 0 &&
		      atomic_read(&fake.shutdowns) == 0 &&
		      atomic_read(&fake.releases) == 0,
		      "create failure records its error without owning a socket"))
		return -EINVAL;

	rk_fake_init(&fake);
	fake.connect_result = -ECONNREFUSED;
	if (rk_expect(!rk_session_start_with_ops(&session, 47023,
						&rk_fake_ops, &fake),
		      "joined session restarts before an injected connect error"))
		return -EINVAL;
	finished = wait_event_timeout(session.wait,
				      !READ_ONCE(session.running), HZ);
	if (rk_expect(finished > 0, "connect-error worker terminates")) {
		rk_session_stop(&session);
		return -EINVAL;
	}
	rk_session_stop(&session);
	memset(&status, 0, sizeof(status));
	rk_session_snapshot(&session, &status);
	return rk_expect(status.last_error == -ECONNREFUSED &&
			 atomic_read(&fake.connect_calls) == 1 &&
			 atomic_read(&fake.shutdowns) == 0 &&
			 atomic_read(&fake.releases) == 1,
			 "connect failure records its error and releases once");
}

static int rk_test_null_create(void)
{
	struct rk_session_status status = { };
	struct rk_session session = { };
	struct rk_fake fake;
	long finished;

	rk_fake_init(&fake);
	fake.null_socket = true;
	if (rk_expect(!rk_session_start_with_ops(&session, 47023,
						 &rk_fake_ops, &fake),
		      "null-create adapter session starts for validation"))
		return -EINVAL;
	finished = wait_event_timeout(session.wait,
				      !READ_ONCE(session.running), HZ);
	if (rk_expect(finished > 0, "null-create worker terminates")) {
		rk_session_stop(&session);
		return -EINVAL;
	}
	rk_session_stop(&session);
	rk_session_snapshot(&session, &status);
	return rk_expect(status.last_error == -EPROTO &&
			 atomic_read(&fake.releases) == 0,
			 "successful create must return a non-null socket");
}

static int rk_test_restart_after_stop(void)
{
	struct rk_session session = { };
	struct rk_fake fake;
	long finished;
	int cycle;

	for (cycle = 0; cycle < 2; cycle++) {
		rk_fake_init(&fake);
		if (rk_expect(!rk_session_start_with_ops(&session, 47023,
							 &rk_fake_ops, &fake),
			      "joined session object can start again"))
			return -EINVAL;
		finished = wait_event_timeout(session.wait,
					      !READ_ONCE(session.running), HZ);
		if (rk_expect(finished > 0, "restart worker reaches EOF")) {
			rk_session_stop(&session);
			return -EINVAL;
		}
		if (rk_expect(rk_session_start_with_ops(&session, 47023,
							&rk_fake_ops,
							&fake) == -EBUSY,
			      "finished worker remains owned until stop joins it")) {
			rk_session_stop(&session);
			return -EINVAL;
		}
		rk_session_stop(&session);
		if (rk_expect(atomic_read(&fake.shutdowns) == 1 &&
			      atomic_read(&fake.releases) == 1,
			      "natural EOF shuts down and releases exactly once"))
			return -EINVAL;
	}
	return 0;
}

__must_check int rk_run_selftests(void)
{
	static const enum rk_fake_block blocked[] = {
		RK_BLOCK_CREATE,
		RK_BLOCK_CONNECT,
		RK_BLOCK_HEADER,
		RK_BLOCK_PAYLOAD,
		RK_BLOCK_SEND,
		RK_BLOCK_DRAIN,
	};
	size_t index;
	int error;

	assertions = 0;
	error = rk_test_argument_validation();
	if (error)
		return error;
	error = rk_test_positive_partials();
	if (error)
		return error;
	error = rk_test_zero_negative_and_impossible();
	if (error)
		return error;
	error = rk_test_oversized(65);
	if (error)
		return error;
	error = rk_test_oversized(65535);
	if (error)
		return error;
	for (index = 0; index < ARRAY_SIZE(blocked); index++) {
		error = rk_test_blocked_stop(blocked[index]);
		if (error)
			return error;
	}
	error = rk_test_null_create();
	if (error)
		return error;
	error = rk_test_worker_failures();
	if (error)
		return error;
	error = rk_test_restart_after_stop();
	if (error)
		return error;
	pr_info("rootkitlings: framed session self-tests passed: %u assertions\n",
		assertions);
	return 0;
}
