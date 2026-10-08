/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_DNS_C2_H
#define ROOTKITLINGS_DNS_C2_H
#include <linux/types.h>

#define DNS_LABEL_MAX       63
#define SIGNAL_BUFFER_SIZE  (DNS_LABEL_MAX + 2)
#define AUTH_TAG_BYTES      8
#define AUTH_TAG_HEX_SIZE   (AUTH_TAG_BYTES * 2 + 1)
#define CAMPAIGN_LABEL_SIZE 17
#define QUERY_TEXT_SIZE     128
#define DNS_LAB_PORT        5300
#define DNS_LAB_SUFFIX      "example"
#define SECONDS_PER_DAY     86400

int auth_tag(const char *signal, char output[AUTH_TAG_HEX_SIZE]);
void campaign_label(u64 bucket, char output[CAMPAIGN_LABEL_SIZE]);
int dns_send(const char *signal, u64 bucket, char *query, size_t query_size,
	     int *sent, u16 *transaction_id);
#endif
