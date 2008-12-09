#include "mdl-elf.h"
#include "mdl.h"

ElfW(Phdr) *mdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type)
{
  MDL_LOG_FUNCTION;
  ElfW(Phdr) *cur;
  int i;
  for (cur = phdr, i = 0; i < phnum; cur++, i++)
    {
      if (cur->p_type == type)
	{
	  return phdr;
	}
    }
  return 0;
}
