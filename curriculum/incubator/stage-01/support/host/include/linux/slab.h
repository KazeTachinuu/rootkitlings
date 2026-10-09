#ifndef ROOTKITLINGS_HOST_SLAB_H
#define ROOTKITLINGS_HOST_SLAB_H

#include <stdbool.h>
#include <stddef.h>

#define GFP_KERNEL 0x10u

void *rk_test_kzalloc(size_t size, unsigned int flags);
void rk_test_kfree(const void *pointer);

#define kzalloc(size, flags) rk_test_kzalloc((size), (flags))
#define kfree(pointer) rk_test_kfree(pointer)

#endif
