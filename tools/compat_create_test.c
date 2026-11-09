// SPDX-License-Identifier: GPL-2.0-only
/*
 * compat_create_test - raw arm32 syscall helper for the create-gate acceptance.
 *
 * Built freestanding (no libc, no dynamic loader) so it can run as a 32-bit
 * process on an arm64 Android kernel and hit the compat syscall table.
 */
#define AT_FDCWD (-100)
#define SYS_exit 1
#define SYS_write 4
#define SYS_mknodat 389
#define SYS_mkdirat 390
#define SYS_symlinkat 395
#define SYS_linkat 396

static long sc6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r7 __asm__("r7") = n;
	register long r0 __asm__("r0") = a;
	register long r1 __asm__("r1") = b;
	register long r2 __asm__("r2") = c;
	register long r3 __asm__("r3") = d;
	register long r4 __asm__("r4") = e;
	register long r5 __asm__("r5") = f;

	__asm__ volatile("svc #0"
			 : "+r"(r0)
			 : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
			 : "memory");
	return r0;
}

static unsigned long slen(const char *s)
{
	unsigned long n = 0;

	while (s[n])
		n++;
	return n;
}

static void puts(const char *s)
{
	sc6(SYS_write, 1, (long)s, (long)slen(s), 0, 0, 0);
}

static char *stpcpy_(char *d, const char *s)
{
	while (*s)
		*d++ = *s++;
	*d = 0;
	return d;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void putnum(long v)
{
	char buf[24];
	char *p = buf + sizeof(buf);
	unsigned long u;
	int neg = v < 0;

	if (neg)
		u = (unsigned long)(-v);
	else
		u = (unsigned long)v;
	*--p = 0;
	do {
		*--p = (char)('0' + (u % 10));
		u /= 10;
	} while (u);
	if (neg)
		*--p = '-';
	sc6(SYS_write, 1, (long)p, (long)slen(p), 0, 0, 0);
}

static void report(const char *name, long rc)
{
	puts(name);
	puts(" rc=");
	putnum(rc);
	puts("\n");
}

void c_start(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	long rc = -22;

	if (argc < 3) {
		puts("usage: mkdir <path> | mknod <path> | symlink <target> <path> | link <old> <new>\n");
		sc6(SYS_exit, 0, 0, 0, 0, 0, 0);
	}

	if (streq(argv[1], "mkdir")) {
		rc = sc6(SYS_mkdirat, AT_FDCWD, (long)argv[2], 0777, 0, 0, 0);
		report("mkdir", rc);
	} else if (streq(argv[1], "mknod")) {
		rc = sc6(SYS_mknodat, AT_FDCWD, (long)argv[2], 0100644, 0, 0, 0);
		report("mknod", rc);
	} else if (streq(argv[1], "symlink") && argc >= 4) {
		rc = sc6(SYS_symlinkat, (long)argv[2], AT_FDCWD, (long)argv[3], 0, 0, 0);
		report("symlink", rc);
	} else if (streq(argv[1], "link") && argc >= 4) {
		rc = sc6(SYS_linkat, AT_FDCWD, (long)argv[2], AT_FDCWD, (long)argv[3], 0, 0);
		report("link", rc);
	} else {
		puts("bad command\n");
	}

	sc6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
