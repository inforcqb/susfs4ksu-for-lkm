/* SPDX-License-Identifier: GPL-2.0-only */
/* Host contract test for susfs_open_view.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "susfs_open_view.h"

static struct susfs_open_view base(enum susfs_open_kind kind)
{
	struct susfs_open_view v;

	memset(&v, 0, sizeof(v));
	v.kind = kind;
	v.flags = O_RDONLY;
	v.valid = true;
	v.mode_valid = true;
	v.resolve_supported = true;
	return v;
}

int main(void)
{
	struct susfs_open_view v;

	assert(susfs_open_decide_core(NULL) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT);
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.flags = O_PATH | O_CREAT | O_EXCL;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.flags = __O_TMPFILE;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT2);
	v.resolve = SUSFS_OPEN_RESOLVE_NO_XDEV;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT2);
	v.resolve = 0x40;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT2);
	v.resolve = SUSFS_OPEN_RESOLVE_BENEATH | SUSFS_OPEN_RESOLVE_IN_ROOT;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT2);
	v.resolve = SUSFS_OPEN_RESOLVE_CACHED;
	v.flags = O_CREAT;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT2);
	v.mode_valid = false;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 0;
	v.caller_uid = 2000;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 0;
	v.caller_uid = 10200;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 1;
	v.caller_uid = 0;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 1;
	v.caller_uid = 0;
	v.caller_in_su_domain = true;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 2;
	v.caller_in_su_domain = true;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 3;
	v.caller_is_umounted_app = true;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 4;
	v.caller_is_umounted = true;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_TAKE);

	v = base(SUSFS_OPEN_OPENAT);
	v.rule_matched = true;
	v.uid_scheme = 9;
	assert(susfs_open_decide_core(&v) == SUSFS_OPEN_PASS);

	puts("susfs_open_view: ok");
	return 0;
}
