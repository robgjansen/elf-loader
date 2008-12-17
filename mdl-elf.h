#ifndef MDL_ELF_H
#define MDL_ELF_H

#include <elf.h>
#include <link.h>
#include "mdl.h"

struct FileInfo
{
  unsigned long dynamic;
  unsigned long interpreter_name;
  unsigned long ro_start;
  unsigned long ro_size;
  unsigned long rw_size;
  unsigned long ro_file_offset;
};

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);
struct StringList *mdl_elf_get_dt_needed (unsigned long load_base, ElfW(Dyn) *dynamic);
char *mdl_elf_search_file (const char *name);
struct MappedFile *mdl_elf_map_single (uint32_t context, const char *filename, const char *name);
int mdl_elf_map_deps (uint32_t context, struct MappedFile *item);
int mdl_elf_file_get_info (uint32_t phnum,
			   ElfW(Phdr) *phdr,
			   struct FileInfo *info);
struct MappedFile *mdl_elf_file_new (unsigned long load_base,
				     const struct FileInfo *info,
				     const char *filename, 
				     const char *name);
struct MappedFileList *mdl_elf_gather_all_deps_breadth_first (struct MappedFile *file);
unsigned long mdl_elf_hash (const char *n);

unsigned long mdl_elf_symbol_lookup (const char *name, 
				     unsigned long hash,
				     struct MappedFileList *scope);

				     

#endif /* MDL_ELF_H */
