#ifndef RK_HOST_LINUX_ALIGN_H
#define RK_HOST_LINUX_ALIGN_H
#define ALIGN(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#endif
