/* SPDX-License-Identifier: GPL-2.0-only */
/* Host contract test for sus_path_view.h. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sus_path_view.h"

static struct sus_path_view base(enum sus_path_face face)
{
	struct sus_path_view v;

	memset(&v, 0, sizeof(v));
	v.face = face;
	v.ino = 42;
	v.s_dev = 3;
	v.i_uid = 10000;
	v.rule_matched = true;
	v.user_rule = true;
	v.hide_from_apps = true;
	v.caller_is_app = true;
	v.caller_uid = 10001;
	return v;
}

int main(void)
{
	struct sus_path_view v;

	assert(sus_path_decide_core(NULL) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_INODE);
	v.rule_matched = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_INODE);
	v.is_resolver = true;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_INODE);
	assert(sus_path_decide_core(&v) == SUS_PATH_HIDE);

	v = base(SUS_PATH_FACE_INODE);
	v.caller_uid = v.i_uid;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_INODE);
	v.caller_is_app = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_INODE);
	v.hide_from_apps = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_HIDE);

	v = base(SUS_PATH_FACE_DIRENT);
	assert(sus_path_decide_core(&v) == SUS_PATH_HIDE);

	v = base(SUS_PATH_FACE_DIRENT);
	v.caller_is_app = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_DIRENT);
	v.user_rule = false;
	v.self_protect = true;
	v.control_visible = true;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	v = base(SUS_PATH_FACE_DIRENT);
	v.user_rule = false;
	v.self_protect = true;
	v.control_visible = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_HIDE);

	v = base(SUS_PATH_FACE_INODE);
	v.self_protect = true;
	v.control_visible = false;
	assert(sus_path_decide_core(&v) == SUS_PATH_HIDE);

	v = base(SUS_PATH_FACE_INODE);
	v.user_rule = false;
	v.self_protect = true;
	v.control_visible = true;
	assert(sus_path_decide_core(&v) == SUS_PATH_PASS);

	puts("sus_path_view: ok");
	return 0;
}
