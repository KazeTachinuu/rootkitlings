#ifndef ROOTKITLINGS_HOST_MODULE_H
#define ROOTKITLINGS_HOST_MODULE_H

#include <stddef.h>

#define module_init(function) \
	int rk_test_module_init(void) { return function(); }
#define module_exit(function) \
	void rk_test_module_exit(void) { function(); }
#define MODULE_LICENSE(value)
#define MODULE_DESCRIPTION(value)

#endif
