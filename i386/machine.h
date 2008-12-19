#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include "mdl.h"

void machine_perform_relocation (struct MappedFile *file,
				 ElfW(Rel) *rel,
				 const char *symbol_name);

#endif /* MACHINE_H */
