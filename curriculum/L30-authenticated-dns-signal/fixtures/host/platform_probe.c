#include <stdbool.h>
#include <stddef.h>

#include "authenticated_dns.h"

#define rk_platform_clear rk_platform_clear_under_test
#include "../../../L28-authenticated-replay-safe-message/fixtures/host/platform.c"
#undef rk_platform_clear

struct clear_probe {
	unsigned int calls;
	unsigned int nonzero_before;
	unsigned int zero_after;
};

static struct clear_probe digest_probe;
static struct clear_probe message_probe;
static struct clear_probe packet_probe;

static struct clear_probe *probe_for(size_t length)
{
	if (length == RK_AUTH_DIGEST_LENGTH)
		return &digest_probe;
	if (length == RK_AUTH_MESSAGE_LENGTH)
		return &message_probe;
	if (length == RK_DNS_SIGNAL_PACKET_LENGTH)
		return &packet_probe;
	return NULL;
}

void rk_test_clear_probe_reset(void)
{
	memset(&digest_probe, 0, sizeof(digest_probe));
	memset(&message_probe, 0, sizeof(message_probe));
	memset(&packet_probe, 0, sizeof(packet_probe));
}

unsigned int rk_test_clear_calls(size_t length)
{
	struct clear_probe *probe = probe_for(length);

	return probe ? probe->calls : 0;
}

unsigned int rk_test_clear_nonzero_before(size_t length)
{
	struct clear_probe *probe = probe_for(length);

	return probe ? probe->nonzero_before : 0;
}

unsigned int rk_test_clear_zero_after(size_t length)
{
	struct clear_probe *probe = probe_for(length);

	return probe ? probe->zero_after : 0;
}

void rk_platform_clear(void *buffer, size_t length)
{
	struct clear_probe *probe = probe_for(length);
	u8 *bytes = buffer;
	bool nonzero = false;
	bool zero = true;
	size_t index;

	if (probe) {
		probe->calls++;
		for (index = 0; index < length; index++)
			if (bytes[index])
				nonzero = true;
	}
	rk_platform_clear_under_test(buffer, length);
	if (!probe)
		return;
	for (index = 0; index < length; index++)
		if (bytes[index])
			zero = false;
	if (nonzero)
		probe->nonzero_before++;
	if (zero)
		probe->zero_after++;
}
