/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * susfs_bti.h - the instructions an indirect branch is allowed to land on.
 *
 * With ARM64_BTI_KERNEL the first instruction of an indirect-call target must be a
 * landing pad, or the CPU raises a BTI exception.  The pads are not only `bti c`:
 * `bti`, `bti j`, `bti jc` and the PAC prologues `paciasp`/`pacibsp` are accepted as
 * well.  The PAC forms matter because a tree built with
 * -mbranch-protection=pac-ret+bti emits `paciasp` as the only pad for a non-leaf
 * function - measured on 6.18 (Xring_o3): both s_show and m_show start with
 * `paciasp`, with no `bti` in front of it.
 *
 * A kprobe replaces the instruction at its address with a breakpoint, so a probe
 * placed on a pad deletes it and every indirect call into that function faults.
 * Callers must therefore skip the pads before arming a probe, and must refuse to
 * arm one when nothing but pads can be found.
 */

#ifndef __SUSFS_BTI_H
#define __SUSFS_BTI_H

#include <linux/types.h>

#define SUSFS_ARM64_BTI		0xd503241fu	/* bti: any type */
#define SUSFS_ARM64_BTI_C	0xd503245fu	/* bti c: call */
#define SUSFS_ARM64_BTI_J	0xd503249fu	/* bti j: jump */
#define SUSFS_ARM64_BTI_JC	0xd50324dfu	/* bti jc: call or jump */
#define SUSFS_ARM64_PACIASP	0xd503233fu	/* paciasp: the pac-ret landing pad */
#define SUSFS_ARM64_PACIBSP	0xd503237fu	/* pacibsp */

/* How many pads a compiler has been seen to emit in front of a body.  One is the
 * usual shape, two (`bti c; paciasp`) exists; the rest keeps a future
 * `bti; bti c; paciasp` from turning into "probe the pad after all". */
#define SUSFS_ARM64_LANDING_PAD_MAX	4

static inline bool susfs_arm64_is_landing_pad(u32 insn)
{
	switch (insn) {
	case SUSFS_ARM64_BTI:
	case SUSFS_ARM64_BTI_C:
	case SUSFS_ARM64_BTI_J:
	case SUSFS_ARM64_BTI_JC:
	case SUSFS_ARM64_PACIASP:
	case SUSFS_ARM64_PACIBSP:
		return true;
	default:
		return false;
	}
}

#endif /* __SUSFS_BTI_H */
