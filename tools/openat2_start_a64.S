/* SPDX-License-Identifier: GPL-2.0-only */
	.global _start
	.type _start, %function
_start:
	mov x0, sp
	bl c_start
	mov x8, #93
	mov x0, #0
	svc #0
	.size _start, .-_start
