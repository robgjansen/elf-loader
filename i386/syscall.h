#include <sys/syscall.h>
/* Linux system call interface for x86 via int 0x80
 * Arguments:
 * %eax System call number.
 * %ebx Arg1
 * %ecx Arg2
 * %edx Arg3
 * %esi Arg4
 * %edi Arg5
 * %ebp Arg6
 *
 * Notes:
 * All registers except %eax must be saved (but ptrace may violate that)
 * return value in %eax, indicates error in range -1,-256
 */                      
#define SYSCALL1(name,arg1)						\
  ({									\
    register unsigned int resultvar;					\
    __asm__ __volatile__ (						\
			  "pushl %%ebx\n\t"				\
			  "movl %2, %%ebx\n\t"				\
			  "movl %1, %%eax\n\t"				\
			  "int $0x80\n\t"				\
			  "popl %%ebx\n\t"				\
			  : "=a" (resultvar)				\
			  : "i" (__NR_##name) , "acdSD" (arg1) : "memory", "cc"); \
    (int) resultvar;							\
  })
#define SYSCALL2(name,arg1,arg2)					\
  ({									\
    register unsigned int resultvar;					\
    __asm__ __volatile__ (						\
			  "pushl %%ebx\n\t"				\
			  "movl %2, %%ebx\n\t"				\
			  "pushl %%ecx\n\t"				\
			  "movl %3, %%ecx\n\t"				\
			  "movl %1, %%eax\n\t"				\
			  "int $0x80\n\t"				\
			  "popl %%ecx\n\t"				\
			  "popl %%ebx\n\t"				\
			  : "=a" (resultvar)				\
			  : "i" (__NR_##name) , "acSD" (arg1),		\
			    "c" (arg2) : "memory", "cc");		\
    (int) resultvar;							\
  })
#define SYSCALL3(name,arg1,arg2,arg3)					\
  ({									\
    register unsigned int resultvar;					\
    __asm__ __volatile__ (						\
			  "pushl %%ebx\n\t"				\
			  "movl %2, %%ebx\n\t"				\
			  "pushl %%ecx\n\t"				\
			  "movl %3, %%ecx\n\t"				\
			  "pushl %%edx\n\t"				\
			  "movl %4, %%edx\n\t"				\
			  "movl %1, %%eax\n\t"				\
			  "int $0x80\n\t"				\
			  "popl %%edx\n\t"				\
			  "popl %%ecx\n\t"				\
			  "popl %%ebx\n\t"				\
			  : "=a" (resultvar)				\
			  : "i" (__NR_##name) , "aSD" (arg1),		\
			    "c" (arg2), "d" (arg3) : "memory", "cc");	\
    (int) resultvar;							\
  })

#define SYSCALL6(name,arg1,arg2,arg3,arg4,arg5,arg6)			\
  ({									\
    register unsigned int resultvar;					\
    int a6 = arg6;							\
    __asm__ __volatile__ (						\
			  "pushl %%ebx\n\t"				\
			  "movl %2, %%ebx\n\t"				\
			  "pushl %%ecx\n\t"				\
			  "movl %3, %%ecx\n\t"				\
			  "pushl %%edx\n\t"				\
			  "movl %4, %%edx\n\t"				\
			  "pushl %%esi\n\t"				\
			  "movl %5, %%esi\n\t"				\
			  "pushl %%edi\n\t"				\
			  "movl %6, %%edi\n\t"				\
			  "pushl %%ebp\n\t"				\
			  "movl %7, %%ebp\n\t"				\
			  "movl %1, %%eax\n\t"				\
			  "int $0x80\n\t"				\
			  "popl %%ebp\n\t"				\
			  "popl %%edi\n\t"				\
			  "popl %%esi\n\t"				\
			  "popl %%edx\n\t"				\
			  "popl %%ecx\n\t"				\
			  "popl %%ebx\n\t"				\
			  : "=a" (resultvar)				\
			  : "i" (__NR_##name) , "a" (arg1),		\
			    "c" (arg2), "d" (arg3), "S" (arg4),		\
			    "D" (arg5), "m" (a6)			\
			  : "memory", "cc");				\
    (int) resultvar;							\
  })
