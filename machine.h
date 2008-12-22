#ifndef MACHINE_H
#define MACHINE_H

#include <elf.h>
#include <link.h>
#include "mdl.h"

void machine_perform_relocation (struct MappedFile *file,
				 ElfW(Rel) *rel,
				 const char *symbol_name);
void machine_start_trampoline (int argc, const char **argv, const char **envp,
			       void (*dl_fini) (void), void (*entry_point) (void));
void machine_finish_tls_setup (unsigned int entry);

#endif /* MACHINE_H */
