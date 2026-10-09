#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rootkit.h"

struct rk_state {
	bool actions_enabled;
	bool vfs_mkdir_seen;
	bool filename_rmdir_seen;
};

static struct kprobe **registered_probes;
static int registered_count;
static unsigned int register_calls;
static struct kprobe **unregistered_probes;
static int unregistered_count;
static unsigned int unregister_calls;

#include <instrumentation/probes.c>

static void fail(const char *message)
{
	fprintf(stderr, "[x] %s\n", message);
	exit(1);
}

int register_kprobes(struct kprobe **probes, int count)
{
	registered_probes = probes;
	registered_count = count;
	register_calls++;
	return 0;
}

void unregister_kprobes(struct kprobe **probes, int count)
{
	unregistered_probes = probes;
	unregistered_count = count;
	unregister_calls++;
}

bool rk_state_actions_enabled(const struct rk_state *state)
{
	return state->actions_enabled;
}

void rk_state_mark_vfs_mkdir_seen(struct rk_state *state)
{
	state->vfs_mkdir_seen = true;
}

void rk_state_mark_filename_rmdir_seen(struct rk_state *state)
{
	state->filename_rmdir_seen = true;
}

int main(void)
{
	struct rk_state state = { 0 };
	struct pt_regs registers = { 0 };

	if (rk_probes_start(NULL) != -EINVAL)
		fail("probe startup accepted a null state");
	if (rk_probes_start(&state) != 0)
		fail("probe catalog did not start");
	if (register_calls != 1 || registered_count != RK_PROBE_COUNT ||
	    registered_count != 2)
		fail("probe catalog was not batch-registered exactly once");
	if (strcmp(rk_probe_name(RK_PROBE_VFS_MKDIR), "vfs_mkdir") != 0 ||
	    strcmp(rk_probe_name(RK_PROBE_FILENAME_RMDIR), "filename_rmdir") != 0)
		fail("registered descriptors do not own the two target identities");
	if (rk_probe_name(RK_PROBE_COUNT) != NULL)
		fail("out-of-range probe identity did not return null");
	if (rk_probes_start(&state) != -EINVAL || register_calls != 1)
		fail("second probe startup was not rejected before registration");

	registered_probes[RK_PROBE_VFS_MKDIR]->pre_handler(
		registered_probes[RK_PROBE_VFS_MKDIR], &registers);
	registered_probes[RK_PROBE_FILENAME_RMDIR]->pre_handler(
		registered_probes[RK_PROBE_FILENAME_RMDIR], &registers);
	if (state.vfs_mkdir_seen || state.filename_rmdir_seen)
		fail("disabled probe handlers changed observation state");
	state.actions_enabled = true;
	registered_probes[RK_PROBE_VFS_MKDIR]->pre_handler(
		registered_probes[RK_PROBE_VFS_MKDIR], &registers);
	registered_probes[RK_PROBE_FILENAME_RMDIR]->pre_handler(
		registered_probes[RK_PROBE_FILENAME_RMDIR], &registers);
	if (!state.vfs_mkdir_seen || !state.filename_rmdir_seen)
		fail("enabled probe handlers did not record both targets");

	rk_probes_stop();
	if (unregister_calls != 1 || unregistered_probes != registered_probes ||
	    unregistered_count != registered_count)
		fail("probe catalog was not batch-unregistered as registered");
	rk_probes_stop();
	if (unregister_calls != 1)
		fail("second probe stop repeated unregistration");

	puts("[+] catalog identity, handlers, and batch lifecycle verified");
	return 0;
}
