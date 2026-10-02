// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

#define TARGET_SCRIPT "/tmp/rk_victim.sh"
#define PAYLOAD_LINE  "# rootkit was here\n"
#define PAYLOAD_LEN   (sizeof(PAYLOAD_LINE) - 1)

static struct proc_dir_entry *proc_entry;
static char original[512];
static ssize_t orig_len;
static bool infected;

static int read_file(const char *path, char *buf, size_t maxlen)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t ret;

	f = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	ret = kernel_read(f, buf, maxlen - 1, &pos);
	filp_close(f, NULL);
	if (ret >= 0)
		buf[ret] = '\0';
	return ret;
}

static int write_file(const char *path, const char *buf, size_t len)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t ret;

	f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (IS_ERR(f))
		return PTR_ERR(f);
	ret = kernel_write(f, buf, len, &pos);
	filp_close(f, NULL);
	return ret < 0 ? ret : 0;
}

static void infect(void)
{
	char buf[512];
	ssize_t len;
	loff_t pos;
	struct file *f;

	len = read_file(TARGET_SCRIPT, original, sizeof(original));
	if (len < 0) {
		pr_err("rk: target not found: %s\n", TARGET_SCRIPT);
		return;
	}
	orig_len = len;

	// Prepend payload to original content
	f = filp_open(TARGET_SCRIPT, O_WRONLY | O_TRUNC, 0);
	if (IS_ERR(f))
		return;
	pos = 0;
	kernel_write(f, PAYLOAD_LINE, PAYLOAD_LEN, &pos);
	kernel_write(f, original, orig_len, &pos);
	filp_close(f, NULL);

	// Verify injection
	len = read_file(TARGET_SCRIPT, buf, sizeof(buf));
	if (len > 0 && strstr(buf, "rootkit was here"))
		infected = true;

	pr_info("rk: infected %s (%s)\n", TARGET_SCRIPT,
		infected ? "ok" : "FAIL");
}

static void disinfect(void)
{
	if (infected && orig_len > 0) {
		write_file(TARGET_SCRIPT, original, orig_len);
		pr_info("rk: restored %s\n", TARGET_SCRIPT);
	}
}

static ssize_t proc_read(struct file *f, char __user *buf,
			  size_t count, loff_t *pos)
{
	char tmp[128];
	int len;

	len = snprintf(tmp, sizeof(tmp), "target=%s\ninfected=%s\n",
		       TARGET_SCRIPT, infected ? "yes" : "no");
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct proc_ops infect_proc_ops = {
	.proc_read = proc_read,
};

static int __init infect_init(void)
{
	infect();

	proc_entry = proc_create("rk_infect", 0444, NULL, &infect_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

static void __exit infect_exit(void)
{
	proc_remove(proc_entry);
	disinfect();
}

module_init(infect_init);
module_exit(infect_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("File infection via content injection");
