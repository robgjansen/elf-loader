#ifndef MDL_ELF_H
#define MDL_ELF_H

#include <elf.h>
#include <link.h>

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);

#endif /* MDL_ELF_H */
