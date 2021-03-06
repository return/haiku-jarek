/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>

__FBSDID("$FreeBSD$");
/* RCSID("$NetBSD: s_logbf.S,v 1.3 1995/05/09 00:15:12 jtc Exp $") */

ENTRY(logbf)
	flds	4(%esp)
	fxtract
	fstp	%st
	ret
END(logbf)

.weak __logbf
.equ __logbf, logbf

	.section .note.GNU-stack,"",%progbits
