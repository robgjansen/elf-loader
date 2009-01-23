#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include "mdl.h"

void machine_perform_relocation (const struct MappedFile *file,
				 const ElfW(Rel) *rel,
				 const ElfW(Sym) *sym,
				 const char *symbol_name);
void machine_start_trampoline (int argc, const char **argv, const char **envp,
			       void (*dl_fini) (void), void (*entry_point) (void));
void machine_insert_trampoline (unsigned long from, unsigned long to);

void machine_tcb_allocate_and_set (unsigned long tcb_size);
void machine_tcb_set_dtv (unsigned long *dtv);
void machine_tcb_set_sysinfo (unsigned long sysinfo);
unsigned long machine_tcb_get (void);
unsigned long *machine_tcb_get_dtv (void);
unsigned long machine_tcb_get_sysinfo (void);

#endif /* MACHINE_H */
