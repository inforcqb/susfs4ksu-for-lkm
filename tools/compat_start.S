/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal ARM32 entry: pass the initial stack (argc/argv) to C. */
	.syntax unified
	.arm
	.global _start
	.type _start, %function
_start:
	mov r0, sp
	bl c_start
	mov r7, #1
	mov r0, #0
	svc #0
	.size _start, .-_start
