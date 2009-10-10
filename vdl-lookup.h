#ifndef VDL_LOOKUP_H
#define VDL_LOOKUP_H

#include "vdl.h"
#include <elf.h>
#include <link.h>
#include <stdbool.h>

struct VdlLookupResult
{
  bool found;
  const struct VdlFile *file;
  const ElfW(Sym) *symbol;
};
enum VdlLookupFlag {
  // indicates whether the symbol lookup is allowed to 
  // find a matching symbol in the main binary. This is
  // typically used to perform the lookup associated
  // with a R_*_COPY relocation.
  VDL_LOOKUP_NO_EXEC = 1
};
int vdl_lookup (struct VdlFile *file,
		const char *name, 
		const char *ver_name,
		const char *ver_filename,
		enum VdlLookupFlag flags,
		struct VdlLookupResult *result);
unsigned long vdl_lookup_local (const struct VdlFile *file, 
				const char *name,
				unsigned long *symbol_size);
int vdl_lookup_with_scope (const struct VdlContext *context,
			   const char *name, 
			   const char *ver_name,
			   const char *ver_filename,
			   struct VdlFileList *scope,
			   struct VdlLookupResult *result);

#endif /* VDL_LOOKUP_H */
