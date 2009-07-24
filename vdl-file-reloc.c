#include "vdl-file-reloc.h"
#include "machine.h"
#include "vdl-log.h"

static ElfW(Vernaux) *
sym_to_vernaux (struct VdlFile *file,
		unsigned long index)
{
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_file_get_dynamic_p (file, DT_VERSYM);
  ElfW(Verneed) *dt_verneed = (ElfW(Verneed)*)vdl_file_get_dynamic_p (file, DT_VERNEED);
  unsigned long dt_verneednum = vdl_file_get_dynamic_v (file, DT_VERNEEDNUM);

  if (dt_versym != 0 && dt_verneed != 0 && dt_verneednum != 0)
    {
      // the same offset used to look in the symbol table (dt_symtab)
      // is an offset in the version table (dt_versym).
      // dt_versym contains a set of 15bit indexes and 
      // 1bit flags packed into 16 bits. When the upper bit is
      // set, the associated symbol is 'hidden', that is, it
      // cannot be referenced from outside of the object.
      ElfW(Half) ver_ndx = dt_versym[index];
      if (ver_ndx & 0x8000)
	{
	  return 0;
	}
      // search the version needed whose vd_ndx is equal to ver_ndx.
      ElfW(Verneed) *cur, *prev;
      for (cur = dt_verneed, prev = 0; 
	   cur != prev; 
	   prev = cur, cur = (ElfW(Verneed) *)(((unsigned long)cur)+cur->vn_next))
	{
	  VDL_LOG_ASSERT (cur->vn_version == 1, "version number invalid for Verneed");
	  ElfW(Vernaux) *cur_aux, *prev_aux;
	  for (cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur)+cur->vn_aux), prev_aux = 0;
	       cur_aux != prev_aux; 
	       prev_aux = cur_aux, cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur_aux)+cur_aux->vna_next))
	    {
	      if (cur_aux->vna_other == ver_ndx)
		{
		  return cur_aux;
		}
	    }
	}
    }
  return 0;
}

static unsigned long
process_rel (struct VdlFile *file, ElfW(Rel) *rel)
{
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_file_get_dynamic_p (file, DT_SYMTAB);
  if (dt_strtab == 0 || dt_symtab == 0)
    {
      return 0;
    }
  ElfW(Sym) *sym = &dt_symtab[ELFW_R_SYM (rel->r_info)];
  ElfW(Vernaux) *ver = sym_to_vernaux (file, ELFW_R_SYM (rel->r_info));
  const char *symbol_name;
  if (sym->st_name == 0)
    {
      symbol_name = 0;
    }
  else
    {
      symbol_name = dt_strtab + sym->st_name;
    }
  unsigned long symbol = machine_reloc_rel (file, rel, sym, ver, symbol_name);
  return symbol;
}

static unsigned long
process_rela (struct VdlFile *file, ElfW(Rela) *rela)
{
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_file_get_dynamic_p (file, DT_SYMTAB);
  if (dt_strtab == 0 || dt_symtab == 0)
    {
      return 0;
    }
  ElfW(Sym) *sym = &dt_symtab[ELFW_R_SYM (rela->r_info)];
  ElfW(Vernaux) *ver = sym_to_vernaux (file, ELFW_R_SYM (rela->r_info));
  const char *symbol_name;
  if (sym->st_name == 0)
    {
      symbol_name = 0;
    }
  else
    {
      symbol_name = dt_strtab + sym->st_name;
    }
  unsigned long symbol = machine_reloc_rela (file, rela, sym, ver, symbol_name);
  return symbol;
}

static void
vdl_file_reloc_jmprel (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);  
  if ((dt_pltrel != DT_REL && dt_pltrel != DT_RELA) ||
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      return;
    }
  if (dt_pltrel == DT_REL)
    {
      int i;
      for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rel)); i++)
	{
	  ElfW(Rel) *rel = &(((ElfW(Rel)*)dt_jmprel)[i]);
	  process_rel (file, rel);
	}
    }
  else
    {
      int i;
      for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rela)); i++)
	{
	  ElfW(Rela) *rela = &(((ElfW(Rela)*)dt_jmprel)[i]);
	  process_rela (file, rela);
	}
    }
}
unsigned long vdl_file_reloc_one_jmprel (struct VdlFile *file, 
				      unsigned long offset)
{
  futex_lock (&g_vdl.futex);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);
  
  if ((dt_pltrel != DT_REL && dt_pltrel != DT_RELA) || 
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      return 0;
    }
  unsigned long symbol;
  if (dt_pltrel == DT_REL)
    {
      ElfW(Rel) *rel = &((ElfW(Rel)*)dt_jmprel)[offset];
      symbol = process_rel (file, rel);
    }
  else
    {
      ElfW(Rela) *rela = &((ElfW(Rela)*)dt_jmprel)[offset];
      symbol = process_rela (file, rela);
    }
  futex_unlock (&g_vdl.futex);
  return symbol;
}


void
vdl_file_reloc_dtrel (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_rel = (ElfW(Rel)*)vdl_file_get_dynamic_p (file, DT_REL);
  unsigned long dt_relsz = vdl_file_get_dynamic_v (file, DT_RELSZ);
  unsigned long dt_relent = vdl_file_get_dynamic_v (file, DT_RELENT);
  if (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relsz/dt_relent; i++)
    {
      ElfW(Rel) *rel = &dt_rel[i];
      process_rel (file, rel);
    }
}

void
vdl_file_reloc_dtrela (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rela) *dt_rela = (ElfW(Rela)*)vdl_file_get_dynamic_p (file, DT_RELA);
  unsigned long dt_relasz = vdl_file_get_dynamic_v (file, DT_RELASZ);
  unsigned long dt_relaent = vdl_file_get_dynamic_v (file, DT_RELAENT);
  if (dt_rela == 0 || dt_relasz == 0 || dt_relaent == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relasz/dt_relaent; i++)
    {
      ElfW(Rela) *rela = &dt_rela[i];
      process_rela (file, rela);
    }
}

void vdl_file_reloc (struct VdlFile *file, int now)
{
  if (file->reloced)
    {
      return;
    }
  file->reloced = 1;

  // relocate dependencies first:
  struct VdlFileList *cur;
  for (cur = file->deps; cur != 0; cur = cur->next)
    {
      vdl_file_reloc (cur->item, now);
    }

  vdl_file_reloc_dtrel (file);
  vdl_file_reloc_dtrela (file);
  if (now)
    {
      // perform full PLT relocs _now_
      vdl_file_reloc_jmprel (file);
    }
  else
    {
      machine_lazy_reloc (file);
    }
}
