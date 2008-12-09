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
	  return cur;
	}
    }
  return 0;
}

struct StringList *mdl_elf_get_dt_needed (unsigned long load_base, ElfW(Dyn) *dynamic)
{
  MDL_LOG_FUNCTION;
  ElfW(Dyn) *cur;
  unsigned long dt_strtab = 0;
  struct StringList *ret = 0;
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_STRTAB)
	{
	  dt_strtab = cur->d_un.d_ptr;
	  break;
	}
    }
  if (dt_strtab == 0)
    {
      return 0;
    }
  for (cur = dynamic; cur->d_tag != DT_NULL; cur++)
    {
      if (cur->d_tag == DT_NEEDED)
	{
	  const char *str = (const char *)(load_base + dt_strtab + cur->d_un.d_val);
	  struct StringList *tmp = mdl_new (struct StringList);
	  tmp->str = mdl_strdup (str);
	  tmp->next = ret;
	  ret = tmp;
	  MDL_LOG_DEBUG ("needed=%s\n", str);
	}
    }
  return ret;
}
