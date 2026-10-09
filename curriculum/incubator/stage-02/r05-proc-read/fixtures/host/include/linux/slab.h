#ifndef FIXTURE_LINUX_SLAB_H
#define FIXTURE_LINUX_SLAB_H

#include <stdlib.h>

#define GFP_KERNEL 0
#define kzalloc(size, flags) calloc(1, (size))
#define kfree(pointer) free(pointer)

#endif
