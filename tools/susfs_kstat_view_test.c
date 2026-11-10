/* SPDX-License-Identifier: GPL-2.0-only */
/* Host contract test for susfs_kstat_view.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "susfs_kstat_view.h"

static struct susfs_kstat_view base(void)
{
	struct susfs_kstat_view v;

	memset(&v, 0, sizeof(v));
	v.table_empty = false;
	v.caller_is_umounted_app = true;
	return v;
}

int main(void)
{
	struct susfs_kstat_view v;

	assert(susfs_kstat_decide_core(NULL) == SUSFS_KSTAT_SKIP);

	v = base();
	assert(susfs_kstat_decide_core(&v) == SUSFS_KSTAT_LOOKUP);

	v = base();
	v.table_empty = true;
	assert(susfs_kstat_decide_core(&v) == SUSFS_KSTAT_SKIP);

	v = base();
	v.caller_is_umounted_app = false;
	assert(susfs_kstat_decide_core(&v) == SUSFS_KSTAT_SKIP);

	puts("susfs_kstat_view: ok");
	return 0;
}
