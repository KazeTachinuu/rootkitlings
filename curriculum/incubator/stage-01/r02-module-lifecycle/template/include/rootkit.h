/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#ifndef ROOTKITLINGS_FAIL_POINT
#error "Kbuild must define ROOTKITLINGS_FAIL_POINT"
#endif

#if ROOTKITLINGS_FAIL_POINT != 0
#error "R02 supports only the inactive failure point"
#endif

/* Later Stage 1 lessons will add failure points and component contracts. */

#endif
