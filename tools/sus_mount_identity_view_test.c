/* SPDX-License-Identifier: GPL-2.0-only */
/* Value contracts only: publication and mount lifetime need kernel coverage. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "sus_mount_identity_view.h"

int main(void)
{
    struct sus_mount_ident e = {
        .s_dev = 7, .root_ino = 99, .devname_is_path = true,
        .devname = "/data/source"
    };
    char long_name[SUS_MOUNT_DEVNAME_MAX + 8];

    assert(!sus_mount_ident_path_core(NULL));
    assert(!sus_mount_ident_path_core(""));
    assert(!sus_mount_ident_path_core("tmpfs"));
    assert(!sus_mount_ident_path_core("/dev/block/dm-0"));
    assert(!sus_mount_ident_path_core("/dev/"));
    assert(sus_mount_ident_path_core("/dev"));
    assert(sus_mount_ident_path_core("/device/file"));
    assert(sus_mount_ident_path_core("/data/source"));
    assert(sus_mount_ident_path_core("/"));

    /* Free entries never match, including a stale retained source path. */
    assert(!sus_mount_ident_match_core(&e, 0, 7, 0, "/data/source"));
    assert(sus_mount_ident_match_core(&e, 99, 7, 99, NULL));
    assert(!sus_mount_ident_match_core(&e, 99, 8, 99, NULL));
    assert(!sus_mount_ident_match_core(&e, 99, 7, 98, NULL));
    /* Source identity is independent of the query device and inode. */
    assert(sus_mount_ident_match_core(&e, 99, 8, 0, "/data/source"));
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, "/data/source/child"));
    e.devname_is_path = false;
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, "/data/source"));
    assert(sus_mount_ident_match_core(&e, 99, 7, 99, "/unrelated"));

    /* Use the acquired publication value, not the possibly changed field. */
    e.root_ino = 0;
    assert(sus_mount_ident_match_core(&e, 99, 7, 99, NULL));

    memset(long_name, 'a', sizeof(long_name));
    long_name[0] = '/';
    long_name[sizeof(long_name) - 1] = '\0';
    memcpy(e.devname, long_name, sizeof(e.devname) - 1);
    e.devname[sizeof(e.devname) - 1] = '\0';
    e.devname_is_path = true;
    e.devname_truncated = false;
    assert(sus_mount_ident_match_core(&e, 99, 8, 0, e.devname));
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, long_name));
    e.devname_truncated = true;
    assert(sus_mount_ident_match_core(&e, 99, 8, 0, long_name));
    long_name[63] = 'b';
    assert(sus_mount_ident_match_core(&e, 99, 8, 0, long_name));
    long_name[62] = 'b';
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, long_name));
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, "/short"));
    assert(!sus_mount_ident_match_core(&e, 99, 8, 0, NULL));

    assert(!sus_mount_id_owned_core(999, 1000));
    assert(sus_mount_id_owned_core(1000, 1000));
    assert(sus_mount_id_owned_core(1001, 1000));
    assert(sus_mount_id_owned_core(0, 0));
    assert(sus_mount_id_owned_core((unsigned int)-1, 1000));
    assert(sus_mount_id_owned_core(UINT_MAX, UINT_MAX));
    assert(!sus_mount_id_owned_core(UINT_MAX - 1, UINT_MAX));
    puts("sus_mount identity view: 32 assertions passed");
    return 0;
}
