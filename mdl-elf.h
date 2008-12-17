#ifndef MDL_ELF_H
#define MDL_ELF_H

#include <elf.h>
#include <link.h>
#include "mdl.h"


ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);
struct StringList *mdl_elf_get_dt_needed (unsigned long load_base, ElfW(Dyn) *dynamic);
char *mdl_elf_search_file (const char *name);
struct MappedFile *mdl_elf_map_single (struct Context *context, const char *filename, const char *name);
int mdl_elf_map_deps (struct Context *context, struct MappedFile *item);
int mdl_elf_file_get_info (uint32_t phnum,
			   ElfW(Phdr) *phdr,
			   struct FileInfo *info);
struct MappedFileList *mdl_elf_gather_all_deps_breadth_first (struct MappedFile *file);
unsigned long mdl_elf_hash (const char *n);
unsigned long mdl_elf_symbol_lookup (const char *name, 
				     unsigned long hash,
				     struct MappedFileList *scope);
void mdl_elf_call_init (struct MappedFile *file);
				     

#endif /* MDL_ELF_H */
