#include "glibc.h"

#define EXPORT __attribute__ ((visibility("default")))

// Set to zero until just before main is invoked
// at which point it must be set to 1. Specifically,
// it is zero during the .init function execution.
int _dl_starting_up EXPORT = 0 ;
// Set to the end of the main stack (the stack allocated
// by the kernel). Must be constant. Is used by libpthread 
// _and_ the ELF loader to make the main stack executable
// when an object which is loaded needs it. i.e., when
// an object is loaded which has a program header of type GNU_STACK
// and the protection of that program header is RWX, we need to
// make sure that all stacks of all threads are executable.
// The stack of the main thread is made executable by incrementally
// changing the protection of every page starting at __libc_stack_end
// When we attempt to change the protection of the bottom
// of the stack, we will fail because there is always a hole below
// the kernel-allocated main stack.
//
// Note: I don't know of any application which actually needs
// an executable stack which is why we don't do much with it.
//
// Implementation note: if you assume that the stack grows downward,
// the easiest way to initialize this variable is to set it to
// __builtin_frame_address(0) from the top-level dynamic loader
// entry point.
//
void *__libc_stack_end EXPORT = 0;
// If set to 1, indicates that we are running with super privileges.
// If so, the ELF loader won't use LD_LIBRARY_PATH, and the libc will
// enable a couple of extra security checks.
// By default, we don't set secure mode, contrary to the libc.
// Theoretically, this variable would be set based on a couple of
// checks on eid/egid, etc.
int __libc_enable_secure = 0;
// Obviously, points to the program argv. I can't figure out why
// and how this symbol is imported by libc.so
char **_dl_argv;

void glibc_startup_finished (void) 
{
  _dl_starting_up = 1;
}

