#ifndef START_TRAMPOLINE_H
#define START_TRAMPOLINE_H

extern void start_trampoline (int argc, const char **argv, const char **envp,
			      void (*dl_fini) (void), void (*entry_point) (void));

#endif /* START_TRAMPOLINE_H */
