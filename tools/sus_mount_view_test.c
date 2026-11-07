/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-side contract checks for sus_mount_view.h (no kernel headers). */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sus_mount_view.h"

static void test_context_bits(void)
{
	assert(sus_mount_view_ctx_valid(SUS_MOUNT_CTX_SHOW));
	assert(sus_mount_view_ctx_valid(SUS_MOUNT_CTX_SCAN));
	assert(sus_mount_view_ctx_valid(SUS_MOUNT_CTX_RECORD));
	assert(!sus_mount_view_ctx_valid((enum sus_mount_context)99));

	assert((sus_mount_view_required(SUS_MOUNT_CTX_SHOW) &
		SUS_MOUNT_V_PATH) == 0);
	assert((sus_mount_view_required(SUS_MOUNT_CTX_SCAN) &
		SUS_MOUNT_V_PATH) != 0);
	assert((sus_mount_view_required(SUS_MOUNT_CTX_RECORD) &
		SUS_MOUNT_V_PATH) != 0);
	assert((sus_mount_view_required(SUS_MOUNT_CTX_SCAN) &
		SUS_MOUNT_V_SU_DOMAIN) == 0);
}

static void test_view_values(void)
{
	struct sus_mount_view v = {0};

	assert(!sus_mount_view_has(&v, SUS_MOUNT_V_MNT_ID));
	v.valid_mask = SUS_MOUNT_V_MNT_ID | SUS_MOUNT_V_DEVID;
	assert(sus_mount_view_has(&v, SUS_MOUNT_V_MNT_ID));
	assert(sus_mount_view_has(&v, SUS_MOUNT_V_DEVID));
	assert(!sus_mount_view_has(&v, SUS_MOUNT_V_PATH));
	assert(!sus_mount_view_has(NULL, SUS_MOUNT_V_MNT_ID));

	v.ctx = SUS_MOUNT_CTX_SCAN;
	v.path_truncated = true;
	v.prefix_matched = true;
	assert(v.prefix_matched && v.path_truncated);
}

int main(void)
{
	test_context_bits();
	test_view_values();
	puts("sus_mount_view: ok");
	return 0;
}
