/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HooKern/hk_exec_nx.h"

#define MAX_TABLES 160
#define ATTRS ((1ULL << 54) | (1ULL << 50) | (1ULL << 51) | 0x703)
static hk_nx_desc *tables[MAX_TABLES], root, *leaf, *chain[HK_NX_LEVELS];
static unsigned int ntables, entries, writes, flushes, cases;
static unsigned int fail_write, ignore_write;
static int partial_write, corrupt_flush;

static hk_nx_desc read_entry(void *entry) { return *(hk_nx_desc *)entry; }
static void *child(hk_nx_desc desc)
{
	unsigned int id = (unsigned int)((desc >> 12) & 0xffff);
	return id && id <= ntables ? tables[id - 1] : NULL;
}
static void *entry_at(void *table, unsigned int index)
{
	assert(index < entries);
	return (hk_nx_desc *)table + index;
}
static int write_entry(void *entry, hk_nx_desc desc)
{
	writes++;
	if (writes == ignore_write)
		return 0;
	if (writes == fail_write) {
		if (partial_write)
			*(hk_nx_desc *)entry = desc;
		return -EFAULT;
	}
	*(hk_nx_desc *)entry = desc;
	return 0;
}
static void flush(void)
{
	flushes++;
	if (corrupt_flush && flushes == 1)
		root |= HK_NX_TABLE;
}
static const struct hk_nx_ops ops = {
	read_entry, child, entry_at, write_entry, flush
};

static hk_nx_desc new_table(void)
{
	unsigned int i;
	assert(ntables < MAX_TABLES);
	tables[ntables] = calloc(entries, sizeof(hk_nx_desc));
	assert(tables[ntables]);
	for (i = 0; i < entries; i++)
		tables[ntables][i] = 0;
	return ((hk_nx_desc)++ntables << 12) | 3;
}

static struct hk_nx_request setup(unsigned int page_shift, unsigned int levels)
{
	struct hk_nx_request req = {0};
	unsigned int i, shift = page_shift + (page_shift - 3) * (levels - 1);
	hk_nx_desc *entry = &root;

	for (i = 0; i < ntables; i++)
		free(tables[i]);
	ntables = writes = flushes = fail_write = ignore_write = 0;
	partial_write = corrupt_flush = 0;
	entries = 1U << (page_shift - 3);
	memset(chain, 0, sizeof(chain));
	req.ops = &ops;
	req.root = &root;
	req.root_shift = shift;
	req.page_shift = page_shift;
	req.addr = 7ULL << page_shift;
	for (i = 0; i < levels; i++) {
		chain[i] = entry;
		if (shift == page_shift) {
			*entry = ATTRS | HK_NX_LEAF;
			leaf = entry;
			break;
		}
		*entry = new_table() | HK_NX_TABLE | (1ULL << 60);
		shift -= page_shift - 3;
		entry = entry_at(child(*entry), (req.addr >> shift) & (entries - 1));
	}
	return req;
}

static void expect(struct hk_nx_request *req, int result)
{
	assert(hk_nx_clear_page(req) == result);
	cases++;
}

int main(void)
{
	struct hk_nx_request req;
	hk_nx_desc saved[HK_NX_LEVELS], before, *neighbour;
	unsigned int i, level, page;

	/* Real 4K/16K/64K geometries, and folded 2/3/4/5-level walks. */
	for (page = 12; page <= 16; page += 2) {
		for (level = 2; level <= (page == 12 ? 5U : 4U); level++) {
			req = setup(page, level);
			for (i = 0; i < level; i++) saved[i] = *chain[i];
			expect(&req, 0);
			assert(req.cleared == level && flushes == 1);
			assert(req.tables_cleared == level - 1);
			for (i = 0; i < level; i++)
				assert(*chain[i] == (saved[i] &
					~(i + 1 == level ? HK_NX_LEAF : HK_NX_TABLE)));
		}
	}

	/* Already executable: no write and no TLB flush. */
	req = setup(12, 3);
	for (i = 0; i < 3; i++) *chain[i] &= ~(HK_NX_TABLE | HK_NX_LEAF);
	expect(&req, 0);
	assert(writes == 0 && flushes == 0);

	/* The Samsung dump shape: leaf PXN=0, PMD PXNTable=1. */
	req = setup(12, 3);
	root &= ~HK_NX_TABLE;
	*leaf &= ~HK_NX_LEAF;
	neighbour = entry_at(child(*chain[1]), 8);
	*neighbour = ATTRS | HK_NX_LEAF;
	before = *neighbour;
	expect(&req, 0);
	assert(req.cleared == 1 && *neighbour == before);
	assert(req.tables_cleared == 1);

	/* A neighbour that relies on the parent must not become executable. */
	req = setup(12, 3);
	neighbour = entry_at(child(*chain[1]), 8);
	*neighbour = ATTRS;
	expect(&req, -EPERM);
	assert(writes == 0 && flushes == 0);

	/* A sibling table's own NX protects even executable-looking leaves. */
	req = setup(12, 3);
	neighbour = entry_at(child(root), 1);
	*neighbour = new_table() | HK_NX_TABLE;
	*(hk_nx_desc *)entry_at(child(*neighbour), 3) = ATTRS;
	before = *neighbour;
	expect(&req, 0);
	assert(*neighbour == before);

	/* Block siblings are inspected; a block on our own path is refused. */
	req = setup(12, 3);
	*(hk_nx_desc *)entry_at(child(root), 1) = 1 | HK_NX_LEAF;
	expect(&req, 0);
	req = setup(12, 3);
	*(hk_nx_desc *)entry_at(child(root), 1) = 1;
	expect(&req, -EPERM);
	assert(writes == 0);
	req = setup(12, 3);
	*chain[1] = 1 | HK_NX_LEAF;
	expect(&req, -EFAULT);
	assert(writes == 0);
	req = setup(12, 3);
	*leaf = 0;
	expect(&req, -EFAULT);
	req = setup(12, 3);
	root = (999ULL << 12) | 3;
	expect(&req, -EFAULT);

	/* First, later and partially completed stores all restore originals. */
	for (i = 1; i <= 3; i++) {
		req = setup(12, 3);
		for (level = 0; level < 3; level++) saved[level] = *chain[level];
		fail_write = i;
		partial_write = 1;
		expect(&req, -EFAULT);
		assert(flushes == 1 && !req.rollback_error);
		for (level = 0; level < 3; level++) assert(*chain[level] == saved[level]);
	}
	req = setup(12, 3);
	before = root;
	ignore_write = 3;
	expect(&req, -EIO);
	assert(root == before && (*leaf & HK_NX_LEAF));
	req = setup(12, 3);
	before = root;
	corrupt_flush = 1;
	expect(&req, -EIO);
	assert(root == before && flushes == 2);

	/* Bounded work in stop_machine, even on a populated upper-level tree. */
	req = setup(12, 3);
	for (i = 1; i < 140; i++) {
		hk_nx_desc desc = new_table();
		unsigned int j;
		*(hk_nx_desc *)entry_at(child(root), i) = desc;
		for (j = 0; j < entries; j++)
			*(hk_nx_desc *)entry_at(child(desc), j) = ATTRS | HK_NX_LEAF;
	}
	expect(&req, -E2BIG);
	assert(writes == 0);
	req = setup(12, 3);
	req.addr++;
	expect(&req, -EINVAL);
	req = setup(12, 3);
	req.root_shift++;
	expect(&req, -EINVAL);

	printf("hk_exec_nx: %u cases passed\n", cases);
	for (i = 0; i < ntables; i++) free(tables[i]);
	return 0;
}
