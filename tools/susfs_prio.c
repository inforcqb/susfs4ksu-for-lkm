// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_prio - call the four priority syscalls from an app uid and print the
 * raw return value, so the priority_uid_filter's -EINVAL can be observed
 * without a libc or a full app.
 *
 * Usage:
 *   susfs_prio getpriority <which> <who>
 *   susfs_prio setpriority <which> <who> <nice>
 *   susfs_prio ioprio_get  <which> <who>
 *   susfs_prio ioprio_set  <which> <who> <ioprio>
 *
 * which: 0 = PROCESS, 1 = PGRP, 2 = USER (getpriority/setpriority);
 *        1 = PROCESS, 2 = PGRP, 3 = USER (ioprio_get/set).
 * Prints "rc=N" (the raw syscall return; -22 is -EINVAL from the filter).
 *
 * Build: same freestanding flags as susfs_sc.
 */

typedef unsigned long u64;
typedef long s64;

#define SYS_write    64
#define SYS_exit     93
#define SYS_getpriority 141
#define SYS_setpriority 140
#define SYS_ioprio_get  31
#define SYS_ioprio_set  30

static long sys2(long n, long a, long b)
{
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
	return x0;
}

static long sys0(long n)
{
	register long x0 asm("x0") = 0;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
	return x0;
}

static long sys3(long n, long a, long b, long c)
{
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x2 asm("x2") = c;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
	return x0;
}

static void out(const char *s)
{
	while (*s) {
		register long x0 asm("x0") = 1;
		register long x1 asm("x1") = (long)s;
		register long x2 asm("x2") = 1;
		register long x8 asm("x8") = SYS_write;

		asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
		s++;
	}
}

static void out_num(long v)
{
	char b[24];
	int i = 0;
	unsigned long u;

	if (v < 0) {
		out("-");
		u = (unsigned long)(-v);
	} else {
		u = (unsigned long)v;
	}
	do {
		b[i++] = '0' + (u % 10);
		u /= 10;
	} while (u);
	while (i)
		out((char[]){b[--i], 0});
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return !*a && !*b;
}

static long parse(const char *s)
{
	long v = 0;
	int neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

void _start_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)&sp[1];
	long rc;

	if (argc < 3) {
		out("usage: susfs_prio <syscall> <which> <who> [value]\n");
		sys0(SYS_exit);
	}
	if (streq(argv[1], "getpriority"))
		rc = sys2(SYS_getpriority, parse(argv[2]), parse(argv[3]));
	else if (streq(argv[1], "setpriority"))
		rc = sys3(SYS_setpriority, parse(argv[2]), parse(argv[3]),
			  argc > 4 ? parse(argv[4]) : 0);
	else if (streq(argv[1], "ioprio_get"))
		rc = sys2(SYS_ioprio_get, parse(argv[2]), parse(argv[3]));
	else if (streq(argv[1], "ioprio_set"))
		rc = sys3(SYS_ioprio_set, parse(argv[2]), parse(argv[3]),
			  argc > 4 ? parse(argv[4]) : 0);
	else {
		out("unknown syscall\n");
		sys0(SYS_exit);
	}
	out("rc=");
	out_num(rc);
	out("\n");
	sys0(SYS_exit);
}

__asm__(
".text\n\
.global _start\n\
_start:\n\
	mov x29, #0\n\
	mov x30, #0\n\
	mov x0, sp\n\
	bl _start_c\n\
	mov x8, #93\n\
	svc #0\n");
