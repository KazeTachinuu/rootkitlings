#ifndef ROOTKITLINGS_HOST_KPROBES_H
#define ROOTKITLINGS_HOST_KPROBES_H

#include <stddef.h>

struct pt_regs {
	int unused;
};

struct kprobe {
	const char *symbol_name;
	int (*pre_handler)(struct kprobe *, struct pt_regs *);
};

int register_kprobes(struct kprobe **probes, int count);
void unregister_kprobes(struct kprobe **probes, int count);

#endif
