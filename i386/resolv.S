	.text
	.align 16
	.globl machine_resolve_trampoline
	.type  machine_resolve_trampoline,@function
machine_resolve_trampoline:
	call vdl_file_reloc_one_plt
	# remove GOT[1] from stack
	addl $4,%esp
	# remove reloc offset from stack
	addl $4,%esp
	# was lookup successful ?
	test %eax,%eax
	jz fail
	# lookup ok, jump in lookup
	jmp *%eax
	# in case of failed lookup, we return immediately.
fail:	ret
