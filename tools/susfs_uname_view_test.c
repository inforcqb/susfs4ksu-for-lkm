/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "susfs_uname_view.h"

int main(void)
{
	struct susfs_uname_values v;
	long results[] = { 0, -14, -38, 1 };
	unsigned int i, enabled, has_buffer;

	for (i = 0; i < sizeof(results) / sizeof(results[0]); i++)
		for (enabled = 0; enabled < 2; enabled++)
			for (has_buffer = 0; has_buffer < 2; has_buffer++)
				assert(susfs_uname_should_apply(results[i], enabled, has_buffer) ==
				       (i == 0 && enabled && has_buffer));
	memset(&v, 0, sizeof(v));
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_EMPTY);
	strcpy(v.release, "default");
	strcpy(v.version, "default");
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_VALID);
	/* Every legal terminator position, including byte 64 (the full ABI width). */
	for (i = 1; i < SUSFS_UNAME_LEN; i++) {
		memset(&v, 'x', sizeof(v));
		v.release[i] = v.version[i] = 0;
		assert(susfs_uname_validate(&v) == SUSFS_UNAME_VALID);
	}
	memset(&v, 'x', sizeof(v));
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_UNTERMINATED);
	v.release[64] = 0;
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_UNTERMINATED);
	v.release[0] = 0;
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_EMPTY);
	memset(&v, 'x', sizeof(v));
	v.version[64] = 0;
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_UNTERMINATED);
	v.version[0] = 0;
	assert(susfs_uname_validate(&v) == SUSFS_UNAME_EMPTY);
	puts("susfs_uname_view: ok (16 result/state cases, 64 terminators, invalid input precedence)");
	return 0;
}
