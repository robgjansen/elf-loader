#ifndef MDL_ELF_H
#define MDL_ELF_H

#include <elf.h>
#include <link.h>
#include "vdl.h"

#if __ELF_NATIVE_CLASS == 32
#define ELFW_R_SYM ELF32_R_SYM
#define ELFW_R_TYPE ELF32_R_TYPE
#define ELFW_ST_BIND(val) ELF32_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF32_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF32_ST_INFO(bind,type)
#else
#define ELFW_R_SYM ELF64_R_SYM
#define ELFW_R_TYPE ELF64_R_TYPE
#define ELFW_ST_BIND(val) ELF64_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF64_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF64_ST_INFO(bind,type)
#endif

ElfW(Phdr) *vdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);
char *vdl_elf_search_file (const char *name);
struct MappedFile *vdl_elf_map_single (struct Context *context, 
				       const char *filename, 
				       const char *name);
int vdl_elf_map_deps (struct MappedFile *item);
int vdl_elf_file_get_info (uint32_t phnum,
			   ElfW(Phdr) *phdr,
			   struct FileInfo *info);
struct MappedFileList *vdl_elf_gather_all_deps_breadth_first (struct MappedFile *file);
unsigned long vdl_elf_hash (const char *n);
void vdl_elf_call_init (struct MappedFile *file);
unsigned long vdl_elf_get_entry_point (struct MappedFile *file);
void vdl_elf_reloc (struct MappedFile *file);
ElfW(Dyn) *vdl_elf_file_get_dynamic (const struct MappedFile *file, unsigned long tag);
struct SymbolMatch
{
  const struct MappedFile *file;
  const ElfW(Sym) *symbol;
};
enum LookupFlag {
  // indicates whether the symbol lookup is allowed to 
  // find a matching symbol in the main binary. This is
  // typically used to perform the lookup associated
  // with a R_*_COPY relocation.
  LOOKUP_NO_EXEC = 1
};
int vdl_elf_symbol_lookup (const char *name, 
			   const struct MappedFile *file,
			   const ElfW(Vernaux) *ver,
			   enum LookupFlag flags,
			   struct SymbolMatch *match);
struct MappedFile *vdl_elf_main_file_new (unsigned long phnum,
					  ElfW(Phdr)*phdr,
					  int argc, 
					  const char **argv, 
					  const char **envp);
unsigned long vdl_elf_symbol_lookup_local (const char *name, const struct MappedFile *file);

void vdl_elf_tls (struct MappedFile *file);
void vdl_elf_tcb_initialize (unsigned long sysinfo);

#endif /* MDL_ELF_H */
