// SPDX-License-Identifier: GPL-2.0
#include "platform.h"
#include "transaction.h"

static const u8 rk_intended[RK_INTENDED_LENGTH] = {
	'r', 'k', 'l', 'i', 'n', 'g', '_', 'd', 'e', 'm', 'o', '\n',
};

static int rk_validate_object(const struct rk_object *object)
{
	if ((object->mode & 0170000) != 0100000 ||
	    (object->mode & 07777) != 0600 || object->uid != 0 ||
	    object->gid != 0 || object->links != 1)
		return -EPERM;
	if (object->length > RK_FILE_MAX)
		return -EFBIG;
	return 0;
}

static int rk_read_exact(struct rk_object *object, u8 *bytes, size_t length)
{
	u8 extra;
	size_t position = 0;

	while (position < length) {
		size_t before = position;
		size_t requested = length - position;
		long count = rk_platform_read(object, bytes + position,
					      requested, &position);

		if (count < 0)
			return (int)count;
		if (count == 0)
			return -EIO;
		if ((size_t)count > requested ||
		    position != before + (size_t)count)
			return -EIO;
	}
	for (;;) {
		size_t before = position;
		long count = rk_platform_read(object, &extra, 1, &position);

		if (count < 0)
			return (int)count;
		if (count == 0)
			return 0;
		if (count > 1 || position != before + (size_t)count)
			return -EIO;
		return -EFBIG;
	}
}

static int rk_take_snapshot(struct rk_snapshot *snapshot,
			    struct rk_object *object)
{
	int error = rk_validate_object(object);

	if (error)
		return error;
	snapshot->retained = *object;
	snapshot->length = (size_t)object->length;
	error = rk_read_exact(&snapshot->retained, snapshot->bytes,
			      snapshot->length);
	if (error)
		return error;
	object->handle = NULL;
	return 0;
}

static int rk_snapshot_matches(const struct rk_snapshot *snapshot,
			       struct rk_object *current)
{
	u8 bytes[RK_FILE_MAX];
	int error;

	if (current->identity != snapshot->retained.identity ||
	    current->mode != snapshot->retained.mode ||
	    current->links != snapshot->retained.links ||
	    current->uid != snapshot->retained.uid ||
	    current->gid != snapshot->retained.gid ||
	    current->length != snapshot->length)
		return -ESTALE;
	error = rk_validate_object(current);
	if (error)
		return -ESTALE;
	error = rk_read_exact(current, bytes, snapshot->length);
	if (error)
		return -ESTALE;
	return memcmp(bytes, snapshot->bytes, snapshot->length) ? -ESTALE : 0;
}

static void rk_preserve(struct rk_transaction *transaction)
{
	transaction->state = RK_TRANSACTION_PRESERVED;
	if (!transaction->warned) {
		transaction->warned = true;
		rk_platform_warn_preserved();
	}
}

static int rk_remove_matching_candidate(struct rk_transaction *transaction)
{
	struct rk_object current = { 0 };
	int error;

	error = rk_platform_open_sibling(&current, RK_STEP_REOPEN_SIBLING);
	if (error)
		return error;
	error = rk_snapshot_matches(&transaction->candidate, &current);
	if (!error)
		error = rk_platform_unlink_sibling(&current);
	rk_platform_close(&current);
	return error;
}

static int rk_write_candidate(struct rk_transaction *transaction)
{
	size_t position = 0;

	memcpy(transaction->candidate.bytes, rk_intended,
	       sizeof(rk_intended));
	transaction->candidate.length = 0;
	while (position < sizeof(rk_intended)) {
		size_t before = position;
		size_t requested = sizeof(rk_intended) - position;
		long count = rk_platform_write(&transaction->candidate.retained,
					       rk_intended + position,
					       requested,
					       &position);

		if (count < 0)
			return (int)count;
		if (count == 0)
			return -EIO;
		if ((size_t)count > requested ||
		    position != before + (size_t)count)
			return -EIO;
		transaction->candidate.length = position;
	}
	return 0;
}

int rk_transaction_apply(struct rk_transaction *transaction)
{
	struct rk_object current_target = { 0 };
	struct rk_object current_sibling = { 0 };
	struct rk_object object = { 0 };
	int cleanup_error;
	int error;

	if (transaction->have_original || transaction->have_candidate ||
	    transaction->exchanged || transaction->state != RK_TRANSACTION_IDLE)
		return -EBUSY;
	memset(transaction, 0, sizeof(*transaction));
	error = rk_platform_open_target(&object, RK_STEP_OPEN_TARGET);
	if (error)
		goto out;
	error = rk_take_snapshot(&transaction->original, &object);
	if (error)
		goto out;
	transaction->have_original = true;
	if (transaction->original.length == sizeof(rk_intended) &&
	    !memcmp(transaction->original.bytes, rk_intended,
		    sizeof(rk_intended))) {
		error = -EALREADY;
		goto out;
	}

	error = rk_platform_create_candidate(&object);
	if (error)
		goto out;
	transaction->candidate.retained = object;
	transaction->candidate.length = 0;
	object.handle = NULL;
	transaction->have_candidate = true;
	error = rk_validate_object(&transaction->candidate.retained);
	if (error || object.length != 0) {
		if (!error)
			error = -ESTALE;
		goto preserve;
	}
	error = rk_write_candidate(transaction);
	if (error)
		goto cleanup;
	error = rk_platform_sync_file(&transaction->candidate.retained);
	if (error)
		goto cleanup;

	error = rk_platform_open_target(&current_target,
					RK_STEP_REOPEN_TARGET);
	if (error)
		goto cleanup;
	error = rk_snapshot_matches(&transaction->original, &current_target);
	if (error)
		goto preserve;
	error = rk_platform_open_sibling(&current_sibling,
					 RK_STEP_REOPEN_SIBLING);
	if (error)
		goto cleanup;
	error = rk_snapshot_matches(&transaction->candidate, &current_sibling);
	if (error)
		goto preserve;
	error = rk_platform_exchange(&current_target, &current_sibling,
				     RK_STEP_EXCHANGE_APPLY);
	if (error)
		goto cleanup;
	transaction->exchanged = true;
	error = rk_platform_sync_directory(RK_STEP_SYNC_APPLY);
	if (error) {
		int restore_error = rk_transaction_restore(transaction);

		if (restore_error)
			rk_preserve(transaction);
		goto out;
	}
	transaction->state = RK_TRANSACTION_APPLIED;
	goto out;

preserve:
	rk_preserve(transaction);
	goto out;
cleanup:
	cleanup_error = rk_remove_matching_candidate(transaction);
	if (cleanup_error)
		rk_preserve(transaction);
out:
	rk_platform_close(&object);
	rk_platform_close(&current_target);
	rk_platform_close(&current_sibling);
	transaction->last_error = error;
	return error;
}

int rk_transaction_restore(struct rk_transaction *transaction)
{
	struct rk_object target = { 0 };
	struct rk_object sibling = { 0 };
	int error;

	if (!transaction->exchanged)
		return 0;
	error = rk_platform_open_target(&target, RK_STEP_REOPEN_TARGET);
	if (error)
		goto preserve;
	error = rk_snapshot_matches(&transaction->candidate, &target);
	if (error)
		goto preserve;
	error = rk_platform_open_sibling(&sibling, RK_STEP_REOPEN_SIBLING);
	if (error)
		goto preserve;
	error = rk_snapshot_matches(&transaction->original, &sibling);
	if (error)
		goto preserve;
	error = rk_platform_exchange(&target, &sibling,
				     RK_STEP_EXCHANGE_RESTORE);
	if (error)
		goto preserve;
	transaction->exchanged = false;
	error = rk_platform_sync_directory(RK_STEP_SYNC_RESTORE);
	if (error)
		goto preserve;
	rk_platform_close(&sibling);
	error = rk_platform_open_sibling(&sibling, RK_STEP_REOPEN_SIBLING);
	if (error)
		goto preserve;
	error = rk_snapshot_matches(&transaction->candidate, &sibling);
	if (error)
		goto preserve;
	error = rk_platform_unlink_sibling(&sibling);
	if (error)
		goto preserve;
	error = rk_platform_sync_directory(RK_STEP_SYNC_UNLINK);
	if (error)
		goto preserve;
	transaction->state = RK_TRANSACTION_RESTORED;
	transaction->last_error = 0;
	goto out;

preserve:
	rk_preserve(transaction);
	transaction->last_error = error;
out:
	rk_platform_close(&target);
	rk_platform_close(&sibling);
	return error;
}

void rk_transaction_dispose(struct rk_transaction *transaction)
{
	if (transaction->have_original)
		rk_platform_close(&transaction->original.retained);
	if (transaction->have_candidate)
		rk_platform_close(&transaction->candidate.retained);
	transaction->have_original = false;
	transaction->have_candidate = false;
}

const char *rk_transaction_state_name(const struct rk_transaction *transaction)
{
	switch (transaction->state) {
	case RK_TRANSACTION_APPLIED:
		return "applied";
	case RK_TRANSACTION_RESTORED:
		return "restored";
	case RK_TRANSACTION_PRESERVED:
		return "preserved";
	case RK_TRANSACTION_IDLE:
	default:
		return "idle";
	}
}
