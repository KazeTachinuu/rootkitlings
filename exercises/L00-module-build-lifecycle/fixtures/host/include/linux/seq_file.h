#ifndef RKLINGS_HOST_SEQ_FILE_H
#define RKLINGS_HOST_SEQ_FILE_H

struct seq_file {
	char text[32];
};

int seq_puts(struct seq_file *output, const char *text);

#endif
