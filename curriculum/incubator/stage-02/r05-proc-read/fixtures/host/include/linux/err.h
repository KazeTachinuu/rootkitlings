#ifndef FIXTURE_LINUX_ERR_H
#define FIXTURE_LINUX_ERR_H

#include <stdint.h>

#define ERR_PTR(error) ((void *)(intptr_t)(error))

#endif
