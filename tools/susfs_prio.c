// SPDX-License-Identifier: GPL-2.0
/*
 * susfs_prio - call the four priority syscalls from an app uid and print the
 * raw return value, so the priority_uid_filter's -EINVAL can be observed
 * without a libc or a full app.
 *
 * Usage:
 *   susfs_prio <syscall> <which> <who> [value]       one call, prints rc=N
 *   susfs_prio bench <syscall> <which> <who> <iters>  loop, prints total/avg ns
 *
 * syscall: getpriority / setpriority / ioprio_get / ioprio_set
 * which:   0 = PROCESS, 1 = PGRP, 2 = USER (getpriority/setpriority);
 *          1 = PROCESS, 2 = PGRP, 3 = USER (ioprio_get/set)
 * Bench measures wall time of <iters> calls in this process (CLOCK_MONOTONIC),
 * so run it under `su <app uid> -c` for the app fast path.
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
#define SYS_clock_gettime 113

#define CLOCK_MONOTONIC 1

struct ts {
	long sec;
	long nsec;
};

static long sys0(long n)
{
	register long x0 asm("x0") = 0;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
	return x0;
}

static long sys2(long n, long a, long b)
{
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
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

static void outs(const char *s)
{
	long n = 0;

	while (s[n])
		n++;
	(void)sys3(SYS_write, 1, (long)s, n);
}

static void out_num(long v)
{
	char b[24];
	int i = 0;
	unsigned long u;
	char neg = 0;

	if (v < 0) {
		neg = 1;
		u = (unsigned long)(-v);
	} else {
		u = (unsigned long)v;
	}
	do {
		b[i++] = '0' + (u % 10);
		u /= 10;
	} while (u);
	if (neg)
		(void)sys3(SYS_write, 1, (long)"-", 1);
	while (i) {
		char c = b[--i];

		(void)sys3(SYS_write, 1, (long)&c, 1);
	}
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

static long do_call(const char *name, long which, long who, long val)
{
	if (streq(name, "getpriority"))
		return sys2(SYS_getpriority, which, who);
	if (streq(name, "setpriority"))
		return sys3(SYS_setpriority, which, who, val);
	if (streq(name, "ioprio_get"))
		return sys2(SYS_ioprio_get, which, who);
	if (streq(name, "ioprio_set"))
		return sys3(SYS_ioprio_set, which, who, val);
	return -1;
}

static void bench(const char *name, long which, long who, long iters)
{
	struct ts t0, t1;
	volatile long sum = 0;
	long i;

	(void)sys2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t0);
	for (i = 0; i < iters; i++)
		sum += do_call(name, which, who, 0);
	(void)sys2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t1);
	{
		unsigned long ns = (unsigned long)((t1.sec - t0.sec) * 1000000000L +
						   (t1.nsec - t0.nsec));

		outs("iters=");
		out_num(iters);
		outs(" total_ns=");
		out_num((long)ns);
		outs(" avg_ns=");
		if (iters)
			out_num((long)(ns / (unsigned long)iters));
		else
			out_num(0);
		outs(" sum=");
		out_num(sum);
		outs("\n");
	}
}

void _start_c(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)&sp[1];

	if (argc < 5) {
		outs("usage: susfs_prio <syscall> <which> <who> [value]\n");
		outs("       susfs_prio bench <syscall> <which> <who> <iters>\n");
		sys0(SYS_exit);
	}
	if (streq(argv[1], "bench")) {
		if (argc < 6) {
			outs("bench needs <iters>\n");
			sys0(SYS_exit);
		}
		bench(argv[2], parse(argv[3]), parse(argv[4]), parse(argv[5]));
	} else {
		long rc = do_call(argv[1], parse(argv[2]), parse(argv[3]),
				  argc > 4 ? parse(argv[4]) : 0);

		outs("rc=");
		out_num(rc);
		outs("\n");
	}
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
