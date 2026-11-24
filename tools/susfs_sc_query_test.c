/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <string.h>
#define SUSFS_SC_HOST_TEST
#include "susfs_sc.c"

static char transcript[9000];
static size_t written;
static int calls, answer;
static long sys4(long n, long a, long b, long c, long d)
{
	if (n == SYS_write) {
		assert(a == 1 && written + c < sizeof(transcript));
		memcpy(transcript + written, (void *)b, c);
		written += c;
		transcript[written] = 0;
		return c;
	}
	assert(n == SYS_reboot && a == KSU_INSTALL_MAGIC1 && b == SUSFS_MAGIC);
	calls++;
	size_t cap = c == CMD_SUSFS_SHOW_ENABLED_FEATURES ? 8192 : 16;
	char *buf = (char *)d;
	int err;
	memcpy(&err, buf + cap, sizeof(err));
	assert(err == ERR_SEED);
	if (!answer)
		return 0;
	memset(buf, 'X', cap); /* Deliberately omit a NUL to check the output limit. */
	err = answer == 1 ? 0 : -12;
	memcpy(buf + cap, &err, sizeof(err));
	return 0;
}

int main(void)
{
	char *commands[] = {"555e1", "555e2", "555e3"};
	for (unsigned i = 0; i < 3; i++) {
		char *args[] = {"susfs_sc", commands[i], "00"};
		size_t cap = i == 1 ? 8192 : 16;
		for (answer = 0; answer < 3; answer++) {
			written = calls = 0;
			assert(sc_main(2, args) == (answer == 1 ? 0 : 1));
			assert(calls == 1);
			char *value = strchr(transcript, 'X');
			if (answer == 1) {
				assert(value && strspn(value, "X") == cap);
				assert(!strcmp(value + cap, "\n"));
			} else {
				assert(!value);
			}
		}
		calls = 0;
		assert(sc_main(3, args) == 2 && !calls);
	}
	return 0;
}
