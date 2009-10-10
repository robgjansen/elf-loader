#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include <stdbool.h>
#include "vdl.h"

struct VdlLookupResult;

bool machine_reloc_is_relative (unsigned long reloc_type);
bool machine_reloc_is_copy (unsigned long reloc_type);
void machine_reloc_without_match (struct VdlFile *file,
				  unsigned long *reloc_addr,
				  unsigned long reloc_type,
				  unsigned long reloc_addend,
				  const ElfW(Sym) *sym);
void machine_reloc_with_match (unsigned long *reloc_addr,
			       unsigned long reloc_type,
			       unsigned long reloc_addend,
			       const struct VdlLookupResult *match);
const char *machine_reloc_type_to_str (unsigned long reloc_type);
bool machine_insert_trampoline (unsigned long from, unsigned long to, unsigned long from_size);
void machine_lazy_reloc (struct VdlFile *file);
uint32_t machine_cmpxchg (uint32_t *val, uint32_t old, uint32_t new);
uint32_t machine_atomic_dec (uint32_t *val);
const char *machine_get_system_search_dirs (void);
const char *machine_get_lib (void);
void *machine_system_mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset);
void machine_thread_pointer_set (unsigned long tp);
unsigned long machine_thread_pointer_get (void);

#endif /* MACHINE_H */
