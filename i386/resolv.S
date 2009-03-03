	.text
	.align 16
	.globl machine_resolve_trampoline
	.type  machine_resolve_trampoline,@function
	# This trampoline is really tricky because it can't assume
	# anything about the calling conventions of the caller or
	# the resolved callee. Specifically, it must be really
	# careful not to clobber any caller-saved registers
machine_resolve_trampoline:
	# first, save all caller-saved registers
	pushl %eax
	pushl %ecx
	pushl %edx
	# push reloc offset on stack
	pushl 16(%esp)
	# push GOT[1] on stack
	pushl 16(%esp)
	call vdl_file_reloc_one_plt
	# was lookup successful ? (position z flag)
	test %eax,%eax
	# save return value in case we have to jump there
	movl %eax,24(%esp)
	# restore caller-saved registers
	movl 16(%esp),%eax
	movl 12(%esp),%ecx
	movl 8(%esp),%edx
	# test the z flag: must do this before addl
	# because addl will clobber the z flag.
	jz fail
	# cleanup all the stack.
	addl $24,%esp
	# jump on return value from vdl_file_reloc_one_plt
	ret
	# cleanup all the stack
fail:	addl $28,%esp
	# return to the caller
	ret
