#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# The early dispatcher (post-fs-data.d) may already have loaded the module; do
# not guess from early_load - run load.sh and let it decide.  It is idempotent
# and reports "already loaded" only when /sys/module/susfs_guard_lkm exists, so
# a failed early attempt is retried here instead of being reported as loaded.
MODDIR=${0%/*}
exec /system/bin/sh "$MODDIR/load.sh"
