#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include "vdl.h"

unsigned long
machine_perform_relocation (struct VdlFile *file,
			    const ElfW(Rel) *rel,
			    const ElfW(Sym) *sym,
			    const ElfW(Vernaux) *ver,
			    const char *symbol_name);
void machine_start_trampoline (void *entry_point_struct, int skip_argc,
			       void (*dl_fini) (void), void (*entry_point) (void));
void machine_insert_trampoline (unsigned long from, unsigned long to);
void machine_lazy_setup (struct VdlFile *file);
void machine_tcb_allocate_and_set (unsigned long tcb_size);
void machine_tcb_set_dtv (unsigned long *dtv);
void machine_tcb_set_sysinfo (unsigned long sysinfo);
unsigned long machine_tcb_get (void);
unsigned long *machine_tcb_get_dtv (void);
unsigned long machine_tcb_get_sysinfo (void);
uint32_t machine_cmpxchg (uint32_t *val, uint32_t old, uint32_t new);
uint32_t machine_atomic_dec (uint32_t *val);
const char *machine_get_system_search_dirs (void);

#endif /* MACHINE_H */
