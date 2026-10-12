#ifndef RKLINGS_HOST_BUG_H
#define RKLINGS_HOST_BUG_H

int rk_test_warn_on(int condition);
#define WARN_ON_ONCE(condition) rk_test_warn_on(!!(condition))

#endif
