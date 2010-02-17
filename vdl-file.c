#include "vdl-file.h"

ElfW(Dyn) *
vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *cur = (ElfW(Dyn)*)file->dynamic;
  while (cur->d_tag != DT_NULL)
    {
      if (cur->d_tag == tag)
	{
	  return cur;
	}
      cur++;
    }
  return 0;
}

unsigned long
vdl_file_get_dynamic_v (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return dyn->d_un.d_val;
}

unsigned long
vdl_file_get_dynamic_p (const struct VdlFile *file, unsigned long tag)
{
  ElfW(Dyn) *dyn = vdl_file_get_dynamic (file, tag);
  if (dyn == 0)
    {
      return 0;
    }
  return file->load_base + dyn->d_un.d_ptr;
}
