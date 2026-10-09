// SPDX-License-Identifier: GPL-2.0
#include "transaction.h"

int rk_transaction_apply(struct rk_transaction *transaction)
{
	(void)transaction;
	return -EINVAL;
}

int rk_transaction_restore(struct rk_transaction *transaction)
{
	(void)transaction;
	return -EINVAL;
}

void rk_transaction_dispose(struct rk_transaction *transaction)
{
	(void)transaction;
}

const char *rk_transaction_state_name(const struct rk_transaction *transaction)
{
	(void)transaction;
	return "unfinished";
}
