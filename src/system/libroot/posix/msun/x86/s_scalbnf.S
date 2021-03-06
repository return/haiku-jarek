/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: s_scalbnf.S,v 1.4 1999/01/02 05:15:40 kristerw Exp $") */

ENTRY(scalbnf)
	fildl	8(%esp)
	flds	4(%esp)
	fscale
	fstp	%st(1)		/* bug fix for fp stack overflow */
	ret
END(scalbnf)

.globl ldexpf
.set	ldexpf,scalbnf

.weak __scalbnf
.equ __scalbnf, scalbnf

.weak __ldexpf
.equ __ldexpf, ldexpf

	.section .note.GNU-stack,"",%progbits
