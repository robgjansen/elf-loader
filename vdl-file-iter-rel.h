#ifndef VDL_ITER_REL_H
#define VDL_ITER_REL_H

#include "vdl.h"
#include <elf.h>
#include <link.h>

void vdl_file_iterate_pltrel (struct VdlFile *file, 
			      void (*cb)(const struct VdlFile *file,
					 const ElfW(Rel) *rel,
					 const ElfW(Sym) *sym,
					 const ElfW(Vernaux) *ver,
					 const char *name));
void vdl_file_iterate_rel (struct VdlFile *file, 
			   void (*cb)(const struct VdlFile *file,
				      const ElfW(Rel) *rel,
				      const ElfW(Sym) *sym,
				      const ElfW(Vernaux) *ver,
				      const char *symbol_name));

#endif /* VDL_ITER_REL_H */
