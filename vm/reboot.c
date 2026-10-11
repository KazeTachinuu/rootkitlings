// Setuid reboot/poweroff for unprivileged VM guests (no CAP_SYS_BOOT).
// argv[0] selects the action: "poweroff" powers off, anything else reboots.
// Never exits, so it is safe to call from PID 1.
#include <unistd.h>
#include <string.h>
#include <sys/reboot.h>

int main(int argc, char **argv)
{
	const char *name = strrchr(argv[0], '/');
	name = name ? name + 1 : argv[0];
	setuid(0);
	sync();
	if (strcmp(name, "poweroff") == 0)
		reboot(RB_POWER_OFF);
	else
		reboot(RB_AUTOBOOT);
	for (;;)
		pause();
}
