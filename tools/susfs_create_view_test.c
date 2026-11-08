/* SPDX-License-Identifier: GPL-2.0-only */
/* Host contract test for susfs_create_view.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "susfs_create_view.h"

static struct susfs_create_view base_view(enum susfs_create_state state)
{
	struct susfs_create_view v;

	memset(&v, 0, sizeof(v));
	v.kind = SUSFS_CREATE_VIEW_MKDIRAT;
	v.state = state;
	v.rule_matched = true;
	v.hide_from_apps = true;
	return v;
}

int main(void)
{
	struct susfs_create_view v;
	struct susfs_create_decision d;

	d = susfs_create_decide_core(NULL);
	assert(d.action == SUSFS_CREATE_ERROR);

	v = base_view(SUSFS_CREATE_ST_ABSENT);
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_PASS);

	v = base_view(SUSFS_CREATE_ST_NOT_NAME);
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_NOT_NAME);

	v = base_view(SUSFS_CREATE_ST_ERROR);
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_ERROR);

	v = base_view(SUSFS_CREATE_ST_EXISTS);
	v.rule_matched = false;
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_PASS);

	v = base_view(SUSFS_CREATE_ST_EXISTS);
	v.hide_from_apps = false;
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_PASS);

	v = base_view(SUSFS_CREATE_ST_EXISTS);
	d = susfs_create_decide_core(&v);
	assert(d.action == SUSFS_CREATE_HIDE);
	assert(d.need_parent_permission);

	v.kind = SUSFS_CREATE_VIEW_LINKAT;
	v.link_old = true;
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_HIDE_OLD);

	v.link_old = false;
	assert(susfs_create_decide_core(&v).action == SUSFS_CREATE_HIDE_NEW);

	puts("susfs_create_view: ok");
	return 0;
}
