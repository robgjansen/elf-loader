# we call this function from C with the following arguments
# (argc, argv, envp, dl_fini, entry_point)
# and we want to jump into the entry point of the main
# binary which expects:
#
# Stack Top -------------
#              argc
#           -------------
#            argv pointer
#           -------------
#            env pointer
#           ------------- 
# edx = dl_fini
# eax = 0
	.text
	.align 16
	.globl machine_start_trampoline
machine_start_trampoline:
	# get dl_fini
	mov 16(%esp), %edx
	# get entry_point
	mov 20(%esp), %esi
	# clear eax
	xorl %eax, %eax
	# push envp
	mov 12(%esp),%edi
	push %edi
	# push argv
	mov 12(%esp),%edi
	push %edi
	# push argc
	mov 12(%esp),%edi
	push %edi
	# call entry point.
	jmp *%esi
