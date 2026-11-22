/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "susfs_avc_view.h"

int main(void)
{
	const u32 su = 3360, replacement = 2655, other = 42;
	u32 source[] = { 0, other, su, replacement };
	unsigned int i, j;

	assert(!susfs_avc_decide_core(NULL));
	/* Both positions are independent, and one audit is a single hit even
	 * when source and target both match. */
	for (i = 0; i < sizeof(source) / sizeof(source[0]); i++) {
		for (j = 0; j < sizeof(source) / sizeof(source[0]); j++) {
			struct susfs_avc_view v = { source[i], source[j], su, replacement };
			bool hit = susfs_avc_decide_core(&v);

			assert(hit == (source[i] == su || source[j] == su));
			assert(v.ssid == (source[i] == su ? replacement : source[i]));
			assert(v.tsid == (source[j] == su ? replacement : source[j]));
		}
	}
	/* Unresolved contexts must never rewrite a valid SID to zero. */
	for (i = 0; i < 3; i++) {
		struct susfs_avc_view v = { su, su, i & 1 ? su : 0,
					  i & 2 ? replacement : 0 };
		assert(!susfs_avc_decide_core(&v));
		assert(v.ssid == su && v.tsid == su);
	}
	{
		struct susfs_avc_view v = { su, su, su, su };
		assert(susfs_avc_decide_core(&v));
		assert(v.ssid == su && v.tsid == su);
	}
	puts("susfs_avc_view: ok (16 SID pairs, unresolved contexts, identical domains)");
	return 0;
}
