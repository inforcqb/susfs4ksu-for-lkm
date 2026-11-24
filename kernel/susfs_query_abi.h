/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SUSFS_QUERY_ABI_H
#define SUSFS_QUERY_ABI_H

#define CMD_SUSFS_SHOW_VERSION                  0x555e1
#define CMD_SUSFS_SHOW_ENABLED_FEATURES         0x555e2
#define CMD_SUSFS_SHOW_VARIANT                  0x555e3
#define SUSFS_ENABLED_FEATURES_SIZE             8192
#define SUSFS_MAX_VERSION_BUFSIZE               16
#define SUSFS_MAX_VARIANT_BUFSIZE               16

struct st_susfs_version {
	char susfs_version[SUSFS_MAX_VERSION_BUFSIZE];
	int err;
};

struct st_susfs_variant {
	char susfs_variant[SUSFS_MAX_VARIANT_BUFSIZE];
	int err;
};

struct st_susfs_enabled_features {
	char enabled_features[SUSFS_ENABLED_FEATURES_SIZE];
	int err;
};

#endif
