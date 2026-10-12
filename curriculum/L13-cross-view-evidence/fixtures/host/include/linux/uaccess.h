#ifndef RKLINGS_HOST_UACCESS_H
#define RKLINGS_HOST_UACCESS_H

#include <stddef.h>

#define __user

unsigned long copy_from_user(void *destination, const void *source,
			     unsigned long count);

#endif
