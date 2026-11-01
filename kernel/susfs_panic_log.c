/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_panic_log.c - best-effort panic log export
 *
 * The vendor hides /sys/fs/pstore, but the kernel still runs kmsg_dump() during
 * panic.  This layer registers a second kmsg dumper, so the same formatted log is
 * written to a file we can read after the device comes back.
 *
 * The write runs in panic context.  It must not allocate; the file is opened at
 * module init and only kernel_write()/vfs_fsync() run in the callback.  This is
 * best-effort by construction - a filesystem that needs to sleep can still hang.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/kprobes.h>
#include <linux/kmsg_dump.h>
#include <linux/version.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "symbol_resolver.h"

#define SUSFS_PANIC_LOG_PATH "/data/local/tmp/susfs_panic.log"

static char *panic_log_path = SUSFS_PANIC_LOG_PATH;
module_param_named(panic_log_path, panic_log_path, charp, 0444);

static int panic_log_enabled = 1;
module_param_named(panic_log_enabled, panic_log_enabled, int, 0644);

static struct file *panic_filp;
static struct kmsg_dumper panic_dumper;
static struct kprobe panic_kp;
static bool dumper_registered;
static bool kprobe_registered;
static atomic_t panic_dumped = ATOMIC_INIT(0);
static char panic_buf[4096];

static void (*kmsg_dump_fn)(enum kmsg_dump_reason reason);
static int (*kmsg_dump_get_buffer_fn)(struct kmsg_dumper *dumper, bool syslog,
				      char *buf, size_t size, size_t *len);
static void (*kmsg_dump_rewind_fn)(struct kmsg_dumper *dumper);

static __nocfi noinline void call_kmsg_dump(enum kmsg_dump_reason reason)
{
	if (kmsg_dump_fn)
		kmsg_dump_fn(reason);
}

static int panic_log_test_set(const char *val, const struct kernel_param *kp)
{
	int enable, rc;

	rc = kstrtoint(val, 0, &enable);
	if (rc)
		return rc;
	if (enable && dumper_registered) {
		atomic_set(&panic_dumped, 0);
		call_kmsg_dump(KMSG_DUMP_PANIC);
	}
	return 0;
}

static const struct kernel_param_ops panic_log_test_ops = {
	.set = panic_log_test_set,
};
module_param_cb(panic_log_test, &panic_log_test_ops, NULL, 0200);

static __nocfi noinline bool call_kmsg_dump_get_buffer(struct kmsg_dumper *dumper,
						       bool syslog, char *buf,
						       size_t size, size_t *len)
{
	if (!kmsg_dump_get_buffer_fn)
		return false;
	return kmsg_dump_get_buffer_fn(dumper, syslog, buf, size, len);
}

static __nocfi noinline void call_kmsg_dump_rewind(struct kmsg_dumper *dumper)
{
	if (kmsg_dump_rewind_fn)
		kmsg_dump_rewind_fn(dumper);
}

/* Called by kmsg_dump() in panic context.  No allocations, no sleeping helpers. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static void panic_log_dumper(struct kmsg_dumper *dumper,
			     struct kmsg_dump_detail *detail)
{
	struct kmsg_dump_iter iter = {};
	size_t len;
	loff_t pos;
	int wrote = 0;

	if (!panic_filp || atomic_xchg(&panic_dumped, 1))
		return;

	pos = 0;
	kmsg_dump_rewind(&iter);
	while (kmsg_dump_get_buffer(&iter, true, panic_buf, sizeof(panic_buf), &len) && len) {
		if (kernel_write(panic_filp, panic_buf, len, &pos) < 0)
			break;
		wrote += len;
		if (wrote > 2 * 1024 * 1024)
			break;
	}
	if (wrote)
		vfs_fsync(panic_filp, 0);
}
#else
static void panic_log_dumper(struct kmsg_dumper *dumper,
			     enum kmsg_dump_reason reason)
{
	size_t len;
	loff_t pos;
	int wrote = 0;

	if (!panic_filp || atomic_xchg(&panic_dumped, 1))
		return;

	pos = 0;
	call_kmsg_dump_rewind(dumper);
	while (call_kmsg_dump_get_buffer(dumper, true, panic_buf, sizeof(panic_buf), &len) && len) {
		if (kernel_write(panic_filp, panic_buf, len, &pos) < 0)
			break;
		wrote += len;
		if (wrote > 2 * 1024 * 1024)
			break;
	}
	if (wrote)
		vfs_fsync(panic_filp, 0);
}
#endif

static int panic_log_pre(struct kprobe *p, struct pt_regs *regs)
{
	/* Run the kernel's own dump path early; the dumper above writes our copy.
	 * The real panic will call kmsg_dump() again; the atomic guard makes our
	 * copy one-shot. */
	if (panic_log_enabled)
		call_kmsg_dump(KMSG_DUMP_PANIC);
	return 0;
}

int susfs_panic_log_init(void)
{
	int rc;

	if (!panic_log_enabled)
		return 0;

	kmsg_dump_fn = (void *)find_kernel_symbol_exact("kmsg_dump");
	kmsg_dump_get_buffer_fn = (void *)find_kernel_symbol_exact("kmsg_dump_get_buffer");
	kmsg_dump_rewind_fn = (void *)find_kernel_symbol_exact("kmsg_dump_rewind");
	if (!kmsg_dump_fn || !kmsg_dump_get_buffer_fn || !kmsg_dump_rewind_fn) {
		pr_warn("susfs_panic_log: kmsg_dump symbols unavailable\n");
		return -ENOENT;
	}

	panic_filp = filp_open(panic_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(panic_filp)) {
		pr_warn("susfs_panic_log: cannot open %s (%ld)\n",
			panic_log_path, PTR_ERR(panic_filp));
		panic_filp = NULL;
		return PTR_ERR(panic_filp);
	}

	memset(&panic_dumper, 0, sizeof(panic_dumper));
	panic_dumper.dump = panic_log_dumper;
	panic_dumper.max_reason = KMSG_DUMP_PANIC;
	rc = kmsg_dump_register(&panic_dumper);
	if (rc) {
		pr_warn("susfs_panic_log: kmsg_dump_register failed %d\n", rc);
		filp_close(panic_filp, NULL);
		panic_filp = NULL;
		return rc;
	}
	dumper_registered = true;

	memset(&panic_kp, 0, sizeof(panic_kp));
	panic_kp.symbol_name = "panic";
	panic_kp.pre_handler = panic_log_pre;
	rc = register_kprobe(&panic_kp);
	if (rc) {
		pr_warn("susfs_panic_log: panic kprobe unavailable %d\n", rc);
		kmsg_dump_unregister(&panic_dumper);
		dumper_registered = false;
		filp_close(panic_filp, NULL);
		panic_filp = NULL;
		return rc;
	}
	kprobe_registered = true;

	pr_info("susfs_panic_log: armed, output %s\n", panic_log_path);
	return 0;
}

void susfs_panic_log_exit(void)
{
	if (kprobe_registered) {
		unregister_kprobe(&panic_kp);
		kprobe_registered = false;
	}
	if (dumper_registered) {
		kmsg_dump_unregister(&panic_dumper);
		dumper_registered = false;
	}
	if (panic_filp) {
		filp_close(panic_filp, NULL);
		panic_filp = NULL;
	}
}
