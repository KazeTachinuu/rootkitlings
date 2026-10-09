#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "transaction.h"

struct fake_file {
	u8 bytes[RK_FILE_MAX + 2];
	size_t length;
	u64 identity;
	unsigned int mode;
	unsigned int links;
	unsigned int uid;
	unsigned int gid;
};

static struct fake_file files[4];
static struct fake_file *target;
static struct fake_file *sibling;
static unsigned int next_file;
static size_t io_limit;
static enum rk_platform_step fail_step;
static int fail_used;
static int warned;
static int malformed_candidate;
static int strange_read;
static int strange_write;
static unsigned long assertions;

enum foreign_change {
	FOREIGN_CONTENT,
	FOREIGN_LENGTH,
	FOREIGN_IDENTITY,
	FOREIGN_MODE,
	FOREIGN_LINKS,
	FOREIGN_UID,
	FOREIGN_GID,
	FOREIGN_REMOVAL,
};

#define CHECK(condition) do { \
	assertions++; \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static struct fake_file *new_file(const u8 *bytes, size_t length)
{
	struct fake_file *file = &files[next_file++];

	memset(file, 0, sizeof(*file));
	if (length)
		memcpy(file->bytes, bytes, length);
	file->length = length;
	file->identity = 100 + next_file;
	file->mode = 0100600;
	file->links = 1;
	return file;
}

static void reset_files(const u8 *bytes, size_t length)
{
	memset(files, 0, sizeof(files));
	next_file = 0;
	target = new_file(bytes, length);
	sibling = NULL;
	io_limit = 0;
	fail_step = RK_STEP_NONE;
	fail_used = 0;
	warned = 0;
	malformed_candidate = 0;
	strange_read = 0;
	strange_write = 0;
}

static void fill_object(struct fake_file *file, struct rk_object *object)
{
	object->handle = file;
	object->identity = file->identity;
	object->length = file->length;
	object->mode = file->mode;
	object->links = file->links;
	object->uid = file->uid;
	object->gid = file->gid;
}

bool rk_platform_should_fail(enum rk_platform_step step)
{
	if (fail_used || fail_step != step)
		return false;
	fail_used = 1;
	return true;
}

int rk_platform_prepare(void) { return 0; }
void rk_platform_finish(void) { }

int rk_platform_open_target(struct rk_object *object, enum rk_platform_step step)
{
	if (rk_platform_should_fail(step))
		return -EIO;
	if (!target)
		return -ENOENT;
	fill_object(target, object);
	return 0;
}

int rk_platform_create_candidate(struct rk_object *object)
{
	if (rk_platform_should_fail(RK_STEP_CREATE_CANDIDATE))
		return -EIO;
	if (sibling)
		return -EEXIST;
	sibling = new_file(NULL, 0);
	if (malformed_candidate == 1)
		sibling->mode = 0100644;
	else if (malformed_candidate == 2)
		sibling->length = 1;
	fill_object(sibling, object);
	return 0;
}

int rk_platform_open_sibling(struct rk_object *object,
			     enum rk_platform_step step)
{
	if (rk_platform_should_fail(step))
		return -EIO;
	if (!sibling)
		return -ENOENT;
	fill_object(sibling, object);
	return 0;
}

void rk_platform_close(struct rk_object *object) { object->handle = NULL; }

long rk_platform_read(struct rk_object *object, u8 *buffer, size_t length,
		      size_t *position)
{
	struct fake_file *file = object->handle;
	size_t count;

	if (rk_platform_should_fail(RK_STEP_READ))
		return -EIO;
	if (strange_read == 1)
		return (long)length + 1;
	if (strange_read == 2)
		return length ? 1 : 0;
	if (strange_read == 3) {
		if (!length)
			return 0;
		*position += 2;
		return 1;
	}
	if (*position >= file->length)
		return 0;
	count = file->length - *position;
	if (count > length)
		count = length;
	if (io_limit && count > io_limit)
		count = io_limit;
	memcpy(buffer, file->bytes + *position, count);
	*position += count;
	return (long)count;
}

long rk_platform_write(struct rk_object *object, const u8 *buffer,
		       size_t length, size_t *position)
{
	struct fake_file *file = object->handle;
	size_t count = length;

	if (rk_platform_should_fail(RK_STEP_WRITE))
		return -EIO;
	if (strange_write == 1)
		return (long)length + 1;
	if (strange_write == 2)
		return length ? 1 : 0;
	if (strange_write == 3) {
		if (!length)
			return 0;
		file->bytes[*position] = *buffer;
		file->length = *position + 1;
		*position += 2;
		return 1;
	}
	if (io_limit && count > io_limit)
		count = io_limit;
	memcpy(file->bytes + *position, buffer, count);
	*position += count;
	if (file->length < *position)
		file->length = *position;
	return (long)count;
}

int rk_platform_sync_file(struct rk_object *object)
{
	(void)object;
	return rk_platform_should_fail(RK_STEP_SYNC_CANDIDATE) ? -EIO : 0;
}

int rk_platform_exchange(struct rk_object *first, struct rk_object *second,
			 enum rk_platform_step step)
{
	struct fake_file *swap;

	(void)first;
	(void)second;
	if (rk_platform_should_fail(step))
		return -EIO;
	swap = target;
	target = sibling;
	sibling = swap;
	return 0;
}

int rk_platform_sync_directory(enum rk_platform_step step)
{
	return rk_platform_should_fail(step) ? -EIO : 0;
}

int rk_platform_unlink_sibling(struct rk_object *expected)
{
	if (rk_platform_should_fail(RK_STEP_UNLINK))
		return -EIO;
	if (!sibling || sibling->identity != expected->identity)
		return -ESTALE;
	sibling = NULL;
	return 0;
}

void rk_platform_warn_preserved(void) { warned++; }

static void expect_applied(const u8 *original, size_t original_length,
			   size_t limit)
{
	static const u8 intended[] = "rootkitlings\n";
	struct rk_transaction transaction = { 0 };

	reset_files(original, original_length);
	io_limit = limit;
	CHECK(rk_transaction_apply(&transaction) == 0);
	CHECK(transaction.state == RK_TRANSACTION_APPLIED);
	CHECK(target && target->length == sizeof(intended) - 1 &&
	      !memcmp(target->bytes, intended, sizeof(intended) - 1));
	CHECK(sibling && sibling->length == original_length &&
	      !memcmp(sibling->bytes, original, original_length));
	CHECK(rk_transaction_apply(&transaction) == -EBUSY);
	CHECK(rk_transaction_restore(&transaction) == 0);
	CHECK(transaction.state == RK_TRANSACTION_RESTORED);
	CHECK(target && target->length == original_length &&
	      !memcmp(target->bytes, original, original_length));
	CHECK(!sibling && !warned);
	rk_transaction_dispose(&transaction);
}

static void test_lengths_and_short_io(void)
{
	u8 binary[RK_FILE_MAX];
	size_t index;

	for (index = 0; index < sizeof(binary); index++)
		binary[index] = (u8)(index * 37);
	expect_applied(binary, 0, 1);
	expect_applied(binary, 1, 1);
	expect_applied(binary, sizeof(binary), 1);
	for (index = 1; index <= RK_FILE_MAX; index++)
		expect_applied(binary, sizeof(binary), index);
}

static void test_rejections(void)
{
	static const u8 intended[] = "rootkitlings\n";
	u8 too_large[RK_FILE_MAX + 1] = { 0 };
	struct rk_transaction transaction = { 0 };

	reset_files(too_large, sizeof(too_large));
	CHECK(rk_transaction_apply(&transaction) == -EFBIG);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files(intended, sizeof(intended) - 1);
	CHECK(rk_transaction_apply(&transaction) == -EALREADY);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"x", 1);
	target->mode = 0100644;
	CHECK(rk_transaction_apply(&transaction) == -EPERM);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"x", 1);
	malformed_candidate = 1;
	CHECK(rk_transaction_apply(&transaction) == -EPERM);
	CHECK(transaction.state == RK_TRANSACTION_PRESERVED);
	CHECK(sibling && warned == 1);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"x", 1);
	malformed_candidate = 2;
	CHECK(rk_transaction_apply(&transaction) == -ESTALE);
	CHECK(transaction.state == RK_TRANSACTION_PRESERVED);
	CHECK(sibling && sibling->length == 1 && warned == 1);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"x", 1);
	sibling = new_file((const u8 *)"occupied", 8);
	CHECK(rk_transaction_apply(&transaction) == -EEXIST);
	CHECK(sibling && sibling->length == 8 &&
	      !memcmp(sibling->bytes, "occupied", 8));
	rk_transaction_dispose(&transaction);
}

static void test_failures(void)
{
	struct rk_transaction transaction;
	int step;

	for (step = RK_STEP_OPEN_TARGET; step <= RK_STEP_SYNC_APPLY; step++) {
		if (step == RK_STEP_PUBLISH)
			continue;
		memset(&transaction, 0, sizeof(transaction));
		reset_files((const u8 *)"original", 8);
		fail_step = step;
		CHECK(rk_transaction_apply(&transaction) < 0);
		CHECK(target && target->length == 8 &&
		      !memcmp(target->bytes, "original", 8));
		CHECK(!sibling);
		rk_transaction_dispose(&transaction);
	}
}

static void make_foreign_change(struct fake_file **name,
				enum foreign_change change)
{
	struct fake_file *file = *name;

	switch (change) {
	case FOREIGN_CONTENT:
		file->bytes[0] ^= 0xff;
		break;
	case FOREIGN_LENGTH:
		file->length--;
		break;
	case FOREIGN_IDENTITY:
		*name = new_file(file->bytes, file->length);
		break;
	case FOREIGN_MODE:
		file->mode = 0100644;
		break;
	case FOREIGN_LINKS:
		file->links = 2;
		break;
	case FOREIGN_UID:
		file->uid = 1;
		break;
	case FOREIGN_GID:
		file->gid = 1;
		break;
	case FOREIGN_REMOVAL:
		*name = NULL;
		break;
	}
}

static void test_foreign_changes(void)
{
	struct rk_transaction transaction;
	int which;
	int change;

	for (which = 0; which < 2; which++) {
		for (change = FOREIGN_CONTENT; change <= FOREIGN_REMOVAL; change++) {
			struct fake_file *target_after_change;
			struct fake_file *sibling_after_change;
			int error;

			memset(&transaction, 0, sizeof(transaction));
			reset_files((const u8 *)"original", 8);
			CHECK(rk_transaction_apply(&transaction) == 0);
			make_foreign_change(which ? &sibling : &target, change);
			target_after_change = target;
			sibling_after_change = sibling;
			error = rk_transaction_restore(&transaction);
			CHECK(error == (change == FOREIGN_REMOVAL ?
				      -ENOENT : -ESTALE));
			CHECK(transaction.state == RK_TRANSACTION_PRESERVED);
			CHECK(warned == 1);
			CHECK(target == target_after_change);
			CHECK(sibling == sibling_after_change);
			rk_transaction_dispose(&transaction);
		}
	}
}

static void test_restore_failures(void)
{
	struct rk_transaction transaction;
	static const enum rk_platform_step steps[] = {
		RK_STEP_READ,
		RK_STEP_REOPEN_TARGET,
		RK_STEP_REOPEN_SIBLING,
		RK_STEP_EXCHANGE_RESTORE,
		RK_STEP_SYNC_RESTORE,
		RK_STEP_UNLINK,
		RK_STEP_SYNC_UNLINK,
	};
	size_t index;

	for (index = 0; index < sizeof(steps) / sizeof(steps[0]); index++) {
		enum rk_platform_step step = steps[index];
		int expected = step == RK_STEP_READ ? -ESTALE : -EIO;

		memset(&transaction, 0, sizeof(transaction));
		reset_files((const u8 *)"original", 8);
		CHECK(rk_transaction_apply(&transaction) == 0);
		fail_step = step;
		fail_used = 0;
		CHECK(rk_transaction_restore(&transaction) == expected);
		CHECK(transaction.state == RK_TRANSACTION_PRESERVED);
		CHECK(target != NULL);
		if (step == RK_STEP_READ || step == RK_STEP_REOPEN_TARGET ||
		    step == RK_STEP_REOPEN_SIBLING ||
		    step == RK_STEP_EXCHANGE_RESTORE) {
			CHECK(target->length == RK_INTENDED_LENGTH);
			CHECK(sibling && sibling->length == 8);
		} else {
			CHECK(target->length == 8 &&
			      !memcmp(target->bytes, "original", 8));
			CHECK(step == RK_STEP_SYNC_UNLINK ? sibling == NULL :
			      sibling && sibling->length == RK_INTENDED_LENGTH);
		}
		rk_transaction_dispose(&transaction);
	}
}

static void test_impossible_io(void)
{
	struct rk_transaction transaction = { 0 };

	reset_files((const u8 *)"original", 8);
	strange_read = 1;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"original", 8);
	strange_read = 2;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"original", 8);
	strange_write = 1;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"original", 8);
	strange_write = 2;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"original", 8);
	strange_read = 3;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(!sibling);
	rk_transaction_dispose(&transaction);

	memset(&transaction, 0, sizeof(transaction));
	reset_files((const u8 *)"original", 8);
	strange_write = 3;
	CHECK(rk_transaction_apply(&transaction) == -EIO);
	CHECK(transaction.state == RK_TRANSACTION_PRESERVED);
	CHECK(sibling && sibling->length == 1 && warned == 1);
	rk_transaction_dispose(&transaction);
}

int main(void)
{
	test_lengths_and_short_io();
	test_rejections();
	test_failures();
	test_foreign_changes();
	test_restore_failures();
	test_impossible_io();
	printf("[+] guarded file transaction: %lu assertions\n", assertions);
	return EXIT_SUCCESS;
}
