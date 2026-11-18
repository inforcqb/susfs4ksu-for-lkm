/* SPDX-License-Identifier: GPL-2.0-only */
/* Freestanding arm64 device test. Load the module with uname disabled first.
 * Uses only the invalid-magic SUSFS command ABI, never a system reboot command. */
typedef unsigned long usize;
struct uts { char sysname[65], nodename[65], release[65], version[65], machine[65], domain[65]; };
struct payload { char release[65], version[65]; int err; };
_Static_assert(sizeof(struct payload) == 136, "SUSFS uname ABI");
_Static_assert(__builtin_offsetof(struct payload, err) == 132, "SUSFS err offset");

void *memset(void *p, int c, usize n)
{
	unsigned char *s = p;
	while (n--) *s++ = c;
	return p;
}

void *memcpy(void *dst, const void *src, usize n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	while (n--) *d++ = *s++;
	return dst;
}

static int equal(const void *a, const void *b, usize n)
{
	const unsigned char *x = a, *y = b;
	while (n--) if (*x++ != *y++) return 0;
	return 1;
}

static long sys4(long nr, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
	return x0;
}

static int failures;
static void check(int ok, const char *message)
{
	usize n = 0;
	const char *prefix = ok ? "PASS " : "FAIL ";
	while (message[n]) n++;
	sys4(64, 1, (long)prefix, 5, 0);
	sys4(64, 1, (long)message, n, 0);
	sys4(64, 1, (long)"\n", 1, 0);
	if (!ok) failures++;
}

static void command(struct payload *p, int expected, const char *name)
{
	struct payload before;
	long rc;

	p->err = 126;
	memcpy(&before, p, sizeof(before));
	rc = sys4(142, 0xDEADBEEFUL, 0xFAFAFAFAUL, 0x55590, (long)p);
	/* Error is carried in the payload; only err may be overwritten. */
	check(rc == 0 && p->err == expected && equal(p, &before, 132), name);
}

static void expect_values(const struct uts *baseline, const char *release,
			  const char *version, const char *name)
{
	struct uts out;
	long rc;

	memset(&out, 0xa5, sizeof(out));
	rc = sys4(160, (long)&out, 0, 0, 0);
	check(rc == 0 && equal(out.release, release, 65) && equal(out.version, version, 65)
	      && equal(out.sysname, baseline->sysname, 65)
	      && equal(out.nodename, baseline->nodename, 65)
	      && equal(out.machine, baseline->machine, 65)
	      && equal(out.domain, baseline->domain, 65), name);
}

int uname_test_main(void)
{
	struct uts baseline;
	struct payload p;
	char expected_release[65], expected_version[65];

	if (sys4(160, (long)&baseline, 0, 0, 0)) return 2;
	memset(&p, 0, sizeof(p));
	command(&p, -22, "empty input -> EINVAL, payload unchanged");
	memset(&p, 'x', sizeof(p));
	command(&p, -36, "unterminated fields -> ENAMETOOLONG");
	p.release[64] = 0;
	command(&p, -36, "unterminated version -> ENAMETOOLONG");
	p.release[0] = 0;
	command(&p, -22, "empty release precedes unterminated version");
	memset(&p, 'x', sizeof(p));
	p.version[64] = 0;
	command(&p, -36, "unterminated release -> ENAMETOOLONG");
	p.version[0] = 0;
	command(&p, -22, "empty version precedes unterminated release");
	expect_values(&baseline, baseline.release, baseline.version, "rejected commands leave uname unchanged");

	memset(&p, 0, sizeof(p));
	memset(p.release, 'r', 64);
	memset(p.version, 'v', 64);
	command(&p, 0, "64-byte fields accepted");
	expect_values(&baseline, p.release, p.version, "full-width readback, other fields unchanged");
	memset(&p, 0, sizeof(p));
	p.release[0] = 'r';
	p.version[0] = 'v';
	command(&p, 0, "short replacement accepted");
	expect_values(&baseline, p.release, p.version, "short replacement zero-pads both tails");
	check(sys4(160, 0, 0, 0, 0) == -14, "uname(NULL) preserves original EFAULT");

	memcpy(expected_release, baseline.release, 65);
	memcpy(expected_version, p.version, 65);
	memset(p.release, 0, 65);
	memcpy(p.release, "default", 7);
	command(&p, 0, "default release with custom version");
	expect_values(&baseline, expected_release, expected_version, "mixed default/custom readback");
	memset(p.version, 0, 65);
	memcpy(p.version, "default", 7);
	command(&p, 0, "both default fields accepted");
	expect_values(&baseline, baseline.release, baseline.version, "default restores current uts values");
	check(failures == 0, "uname contract complete");
	return failures ? 1 : 0;
}

__asm__(".text\n.global _start\n.type _start,%function\n_start:\n"
	"mov x29, #0\nbl uname_test_main\nmov x8, #93\nsvc #0\n");
