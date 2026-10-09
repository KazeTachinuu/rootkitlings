/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_WRITABLE_H
#define ROOTKITLINGS_WRITABLE_H

struct sk_buff;

int rk_skb_make_writable(struct sk_buff *skb, unsigned int write_length);
void rk_skb_fail_next_writable(void);

#endif
