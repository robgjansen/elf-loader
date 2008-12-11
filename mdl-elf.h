#ifndef MDL_ELF_H
#define MDL_ELF_H

#include <elf.h>
#include <link.h>
#include "mdl.h"

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);

struct StringList *mdl_elf_get_dt_needed (unsigned long load_base, ElfW(Dyn) *dynamic);
char *mdl_elf_search_file (const char *name);
struct MappedFile *mdl_elf_load_single (const char *filename, const char *name);

#endif /* MDL_ELF_H */
