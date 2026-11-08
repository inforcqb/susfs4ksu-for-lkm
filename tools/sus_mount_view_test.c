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
	sus_mount_view_set_prefix(&v, true);
	assert(v.prefix_matched && v.path_truncated);
}

static void test_record_decision(void)
{
	struct sus_mount_view v = {0};

	assert(sus_mount_record_decide(&v, true, true) == SUS_MOUNT_RECORD_SKIP);
	v.prefix_matched = true;
	assert(sus_mount_record_decide(&v, false, false) == SUS_MOUNT_RECORD_TAKE);
}

static void test_path_set(void)
{
	struct sus_mount_view v = {0};
	char path[600];

	memset(path, 'a', sizeof(path));
	path[sizeof(path) - 1] = 0;
	sus_mount_view_set_path(&v, path, sizeof(path) - 1);
	assert(sus_mount_view_has(&v, SUS_MOUNT_V_PATH));
	assert(v.path_truncated);
	assert(v.path_len == SUS_MOUNT_VIEW_PATH_MAX - 1);
	assert(v.mountpoint_path[v.path_len] == 0);
	assert(v.source == SUS_MOUNT_SRC_PATH);
}

static void test_show_decision(void)
{
	struct sus_mount_view v = {0};

	assert(sus_mount_show_decide(&v, false, false) == SUS_MOUNT_SHOW_VISIBLE);
	assert(sus_mount_show_decide(&v, false, true) == SUS_MOUNT_SHOW_VISIBLE);
	assert(sus_mount_show_decide(&v, true, true) == SUS_MOUNT_SHOW_VISIBLE);
	assert(sus_mount_show_decide(&v, true, false) == SUS_MOUNT_SHOW_HIDE);
}

int main(void)
{
	test_context_bits();
	test_view_values();
	test_record_decision();
	test_path_set();
	test_show_decision();
	puts("sus_mount_view: ok");
	return 0;
}
