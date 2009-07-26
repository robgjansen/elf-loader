#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include "vdl.h"

unsigned long machine_reloc_rel (struct VdlFile *file, const ElfW(Rel) *rel,
				 const ElfW(Sym) *sym, const ElfW(Vernaux) *ver,
				 const char *symbol_name);
unsigned long machine_reloc_rela (struct VdlFile *file, const ElfW(Rela) *rela,
				  const ElfW(Sym) *sym, const ElfW(Vernaux) *ver,
				  const char *symbol_name);
void machine_insert_trampoline (unsigned long from, unsigned long to);
void machine_lazy_reloc (struct VdlFile *file);
uint32_t machine_cmpxchg (uint32_t *val, uint32_t old, uint32_t new);
uint32_t machine_atomic_dec (uint32_t *val);
const char *machine_get_system_search_dirs (void);
void *machine_system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
void machine_thread_pointer_set (unsigned long tp);
unsigned long machine_thread_pointer_get (void);

#endif /* MACHINE_H */
