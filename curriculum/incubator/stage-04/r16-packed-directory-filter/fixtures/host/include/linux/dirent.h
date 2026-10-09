#ifndef RK_HOST_LINUX_DIRENT_H
#define RK_HOST_LINUX_DIRENT_H
#include <linux/types.h>
struct linux_dirent64 {
	u64 d_ino;
	s64 d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};
#endif
