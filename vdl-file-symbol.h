#ifndef VDL_FILE_SYMBOL_H
#define VDL_FILE_SYMBOL_H

#include "vdl.h"
#include <elf.h>
#include <link.h>

struct SymbolMatch
{
  const struct VdlFile *file;
  const ElfW(Sym) *symbol;
};
enum LookupFlag {
  // indicates whether the symbol lookup is allowed to 
  // find a matching symbol in the main binary. This is
  // typically used to perform the lookup associated
  // with a R_*_COPY relocation.
  LOOKUP_NO_EXEC = 1
};
int vdl_file_symbol_lookup (struct VdlFile *file,
			    const char *name, 
			    const char *ver_name,
			    const char *ver_filename,
			    enum LookupFlag flags,
			    struct SymbolMatch *match);
unsigned long vdl_file_symbol_lookup_local (const struct VdlFile *file, 
					    const char *name,
					    unsigned long *symbol_size);
int vdl_file_symbol_lookup_scope (const struct VdlContext *context,
				  const char *name, 
				  const char *ver_name,
				  const char *ver_filename,
				  struct VdlFileList *scope,
				  struct SymbolMatch *match);

#endif /* VDL_FILE_SYMBOL_H */
