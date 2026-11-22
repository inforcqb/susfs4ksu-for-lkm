/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include "susfs_hide_view.h"

int main(void)
{
	/* Existing matching contract: literal prefixes, lowercase substrings,
	 * and no implicit case folding or token-boundary requirement. */
	assert(susfs_hide_name_decide_core("ksu_allow_uid"));
	assert(susfs_hide_name_decide_core("__ksu_allow_uid"));
	assert(susfs_hide_name_decide_core("susfs_guard_lkm"));
	assert(susfs_hide_name_decide_core("anon_ksu_ioctl.cfi_jt"));
	assert(susfs_hide_name_decide_core("prefix_susfs_suffix"));
	assert(susfs_hide_name_decide_core("is_manager_check"));
	assert(susfs_hide_name_decide_core("setup_selinux"));
	assert(susfs_hide_name_decide_core("setup_selinux_extra"));
	assert(susfs_hide_name_decide_core("getenforce"));
	assert(susfs_hide_name_decide_core("getenforce_extra"));
	assert(susfs_hide_name_decide_core("is_zygote"));
	assert(!susfs_hide_name_decide_core(""));
	assert(!susfs_hide_name_decide_core("is_manager"));
	assert(!susfs_hide_name_decide_core("setup_selinu"));
	assert(!susfs_hide_name_decide_core("x_setup_selinux"));
	assert(!susfs_hide_name_decide_core("KSU_ALLOW_UID"));
	assert(!susfs_hide_name_decide_core("SUSFS_TEST"));
	assert(!susfs_hide_name_decide_core("do_sys_openat2"));
	puts("susfs_hide_view: ok (18 existing prefix/substring/boundary cases)");
	return 0;
}
