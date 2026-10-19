#include "rootkit.h"

unsigned long rk_private_state_size(void)
{
	return sizeof(struct rk_state);
}
