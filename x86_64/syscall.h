#include <sys/syscall.h>
/* Linux system call interface for x86_64 via syscall
 * Arguments:
 * %rax System call number.
 * %rdi Arg1
 * %rsi Arg2
 * %rdx Arg3
 * %r10 Arg4
 * %r8 Arg5
 * %r9 Arg6
 * %rax return value (-4095 to -1 is an error: -errno)
 *
 * clobbered: all above and %rcx and %r11
 */                      
#define SYSCALL1(name,arg1)						\
  ({									\
    register unsigned long int resultvar;				\
    long int _arg1 = (long int) (arg1);					\
    register long int _a1 asm ("rdi") = _arg1;				\
    __asm__ __volatile__ (						\
			  "syscall\n\t"					\
			  : "=a" (resultvar)				\
			  : "0" (__NR_##name) , "r" (_a1)		\
			  : "memory", "cc", "r11", "rcx");		\
    (long int) resultvar;						\
  })
#define SYSCALL2(name,arg1,arg2)					\
  ({									\
    register unsigned long int resultvar;				\
    long int _arg1 = (long int) (arg1);					\
    register long int _a1 asm ("rdi") = _arg1;				\
    long int _arg2 = (long int) (arg2);					\
    register long int _a2 asm ("rsi") = _arg2;				\
    __asm__ __volatile__ (						\
			  "syscall\n\t"					\
			  : "=a" (resultvar)				\
			  : "0" (__NR_##name) , "r" (_a1), "r" (_a2)	\
			  : "memory", "cc", "r11", "rcx");		\
    (long int) resultvar;						\
  })
#define SYSCALL3(name,arg1,arg2,arg3)					\
  ({									\
    register unsigned long int resultvar;				\
    long int _arg1 = (long int) (arg1);					\
    register long int _a1 asm ("rdi") = _arg1;				\
    long int _arg2 = (long int) (arg2);					\
    register long int _a2 asm ("rsi") = _arg2;				\
    long int _arg3 = (long int) (arg3);					\
    register long int _a3 asm ("rdx") = _arg3;				\
    __asm__ __volatile__ (						\
			  "syscall\n\t"					\
			  : "=a" (resultvar)				\
			  : "0" (__NR_##name) , "r" (_a1), "r" (_a2),	\
			    "r" (_a3) : "memory",			\
			    "cc", "r11", "rcx");			\
    (long int) resultvar;						\
  })

#define SYSCALL6(name,arg1,arg2,arg3,arg4,arg5,arg6)			\
  ({									\
    register unsigned long int resultvar;				\
    long int _arg1 = (long int) (arg1);					\
    register long int _a1 asm ("rdi") = _arg1;				\
    long int _arg2 = (long int) (arg2);					\
    register long int _a2 asm ("rsi") = _arg2;				\
    long int _arg3 = (long int) (arg3);					\
    register long int _a3 asm ("rdx") = _arg3;				\
    long int _arg4 = (long int) (arg4);					\
    register long int _a4 asm ("r10") = _arg4;				\
    long int _arg5 = (long int) (arg5);					\
    register long int _a5 asm ("r8") = _arg5;				\
    long int _arg6 = (long int) (arg6);					\
    register long int _a6 asm ("r9") = _arg6;				\
    __asm__ __volatile__ (						\
			  "syscall\n\t"					\
			  : "=a" (resultvar)				\
			  : "0" (__NR_##name) , "r" (_a1), "r" (_a2),	\
			    "r" (_a3), "r" (_a4), "r" (_a5), "r" (_a6):	\
			    "memory", "cc", "r11", "rcx");		\
    (long int) resultvar;						\
  })
