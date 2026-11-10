// SPDX-License-Identifier: GPL-2.0-only
/*
 * openat2_test - freestanding native/compat openat/openat2 helper.
 *
 * Built for arm64 and armv7 with raw syscalls so both the native and the
 * compat open entries can be exercised on an arm64 device.
 */
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_LARGEFILE 0400000
#define RESOLVE_NO_XDEV 0x01

#ifdef __aarch64__
#define SYS_read 63
#define SYS_write 64
#define SYS_close 57
#define SYS_exit 93
#define SYS_openat 56
#define SYS_openat2 437
#else
#define SYS_read 3
#define SYS_write 4
#define SYS_close 6
#define SYS_exit 1
#define SYS_openat 322
#define SYS_openat2 437
#endif

struct open_how {
	unsigned long long flags;
	unsigned long long mode;
	unsigned long long resolve;
};

static long sc6(long n, long a, long b, long c, long d, long e, long f)
{
#ifdef __aarch64__
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
			 : "memory");
	return x0;
#else
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
#endif
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
	unsigned long u = (unsigned long)v;
	int neg = v < 0;

	if (neg)
		u = (unsigned long)(-v);
	*--p = 0;
	do {
		unsigned long d = u & 0xf;

		*--p = (char)(d < 10 ? '0' + d : 'a' + d - 10);
		u >>= 4;
	} while (u);
	if (neg)
		*--p = '-';
	puts("0x");
	sc6(SYS_write, 1, (long)p, (long)slen(p), 0, 0, 0);
}

static void report(const char *name, long rc)
{
	puts(name);
	puts(" rc=");
	putnum(rc);
	puts("\n");
}

static void show_file(int fd)
{
	char buf[64];
	long n;

	n = sc6(SYS_read, fd, (long)buf, sizeof(buf) - 1, 0, 0, 0);
	if (n < 0) {
		report("read", n);
		return;
	}
	buf[n] = 0;
	puts("content=");
	puts(buf);
	puts("\n");
}

void c_start(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct open_how how = { .flags = O_RDONLY, .mode = 0, .resolve = 0 };
	long fd;

	if (argc < 3) {
		puts("usage: openat|openat2|openat2-noxdev <path>\n");
		sc6(SYS_exit, 0, 0, 0, 0, 0, 0);
	}

	if (streq(argv[1], "openat")) {
		fd = sc6(SYS_openat, AT_FDCWD, (long)argv[2],
			 O_RDONLY | O_LARGEFILE, 0, 0, 0);
		report("openat", fd);
	} else if (streq(argv[1], "openat2") || streq(argv[1], "openat2-noxdev")) {
		if (streq(argv[1], "openat2-noxdev"))
			how.resolve = RESOLVE_NO_XDEV;
		fd = sc6(SYS_openat2, AT_FDCWD, (long)argv[2], (long)&how,
			 sizeof(how), 0, 0);
		report("openat2", fd);
	} else {
		puts("bad command\n");
		fd = -1;
	}

	if (fd >= 0) {
		show_file((int)fd);
		sc6(SYS_close, fd, 0, 0, 0, 0, 0);
	}
	sc6(SYS_exit, 0, 0, 0, 0, 0, 0);
}
