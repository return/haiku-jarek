/*
 * Based on code written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#include <arch/x86/asm.h>
__FBSDID("$FreeBSD$")

ENTRY(floorl)
	pushl	%ebp
	movl	%esp,%ebp
	subl	$8,%esp

	fstcw	-4(%ebp)		/* store fpu control word */
	movw	-4(%ebp),%dx
	orw	$0x0400,%dx		/* round towards -oo */
	andw	$0xf7ff,%dx
	movw	%dx,-8(%ebp)
	fldcw	-8(%ebp)		/* load modfied control word */

	fldt	8(%ebp)			/* round */
	frndint

	fldcw	-4(%ebp)		/* restore original control word */

	leave
	ret
END(floorl)

.weak __floorl
.equ __floorl, floorl

	.section .note.GNU-stack,"",%progbits
