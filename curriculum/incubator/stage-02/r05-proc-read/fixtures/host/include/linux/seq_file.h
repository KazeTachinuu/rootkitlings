#ifndef FIXTURE_LINUX_SEQ_FILE_H
#define FIXTURE_LINUX_SEQ_FILE_H

#include <stddef.h>

struct seq_file {
	void *private;
	char *buffer;
	size_t capacity;
};

int seq_printf(struct seq_file *output, const char *format, ...);

#endif
