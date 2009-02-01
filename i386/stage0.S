# The name of this symbol will implicitely make
# it the entry point of the generated binary
	.text
	.align 16
	.globl stage0
	.type  stage0,@function
stage0:
	movl %esp,%esi
	pushl %esi
	pushl %esi
	pushl %esi
	pushl %esp
	call stage1
	movl 0(%eax),%esp
	movl 4(%eax),%esi
	movl 8(%eax),%edx
	xorl %eax, %eax
	jmp *%esi
