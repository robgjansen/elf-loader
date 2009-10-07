#include "vdl-file-reloc.h"
#include "machine.h"
#include "vdl-log.h"
#include "vdl-sort.h"
#include "vdl-file-list.h"
#include "vdl-file-symbol.h"
#include "vdl-utils.h"
#include <stdbool.h>

static bool
sym_to_ver_req (struct VdlFile *file,
		unsigned long index,
		ElfW(Verneed) **pverneed,
		ElfW(Vernaux) **pvernaux)
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
		  *pverneed = cur;
		  *pvernaux = cur_aux;
		  return true;
		}
	    }
	}
    }
  return false;
}

static unsigned long
do_process_reloc (struct VdlFile *file, 
		  unsigned long reloc_type, unsigned long *reloc_addr,
		  unsigned long reloc_addend, unsigned long reloc_sym)
{
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_file_get_dynamic_p (file, DT_SYMTAB);
  if (dt_strtab == 0 || dt_symtab == 0)
    {
      return 0;
    }
  ElfW(Sym) *sym = &dt_symtab[reloc_sym];

  VDL_LOG_FUNCTION ("file=%s, type=%s, addr=0x%lx, addend=0x%lx, sym=%s", 
		    file->filename, machine_reloc_type_to_str (reloc_type), 
		    reloc_addr, reloc_addend, reloc_sym == 0?"0":dt_strtab + sym->st_name);

  if (!machine_reloc_is_relative (reloc_type) &&
      sym->st_name != 0)
    {
      const char *symbol_name = dt_strtab + sym->st_name;
      int flags = 0;
      if (machine_reloc_is_copy (reloc_type))
	{
	  // for R_*_COPY relocations, we must use the
	  // LOOKUP_NO_EXEC flag to avoid looking up the symbol
	  // in the main binary.
	  flags |= LOOKUP_NO_EXEC;
	}
      struct SymbolMatch match;
      const char *ver_name = 0;
      const char *ver_filename = 0;
      ElfW(Verneed) *verneed;
      ElfW(Vernaux) *vernaux;
      if (sym_to_ver_req (file, reloc_sym, &verneed, &vernaux))
	{
	  ver_name = dt_strtab + vernaux->vna_name;
	  ver_filename = dt_strtab + verneed->vn_file;
	}

      if (!vdl_file_symbol_lookup (file, symbol_name, ver_name, ver_filename, flags, &match))
	{
	  if (ELFW_ST_BIND (sym->st_info) == STB_WEAK)
	    {
	      // The symbol we are trying to resolve is marked weak so,
	      // if we can't find it, it's not an error.
	    }
	  else
	    {
	      // This is really a hard failure. We do not assert
	      // to emulate the glibc behavior
	      VDL_LOG_SYMBOL_FAIL (symbol_name, file);
	    }
	  return 0;
	}
      VDL_LOG_SYMBOL_OK (symbol_name, file, match);
      if (machine_reloc_is_copy (reloc_type))
	{
	  // we handle R_*_COPY relocs ourselves
	  VDL_LOG_ASSERT (match.symbol->st_size == sym->st_size,
			  "Symbols don't have the same size: likely a recipe for disaster.");
	  vdl_utils_memcpy (reloc_addr, 
			    (void*)(match.file->load_base + match.symbol->st_value),
			    match.symbol->st_size);
	}
      else
	{
	  machine_reloc_with_match (reloc_addr, reloc_type, reloc_addend, &match);
	}
    }
  else
    {
      machine_reloc_without_match (file, reloc_addr, reloc_type, reloc_addend, sym);
    }
  return *reloc_addr;
}

static unsigned long
process_rel (struct VdlFile *file, ElfW(Rel) *rel)
{
  unsigned long reloc_type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (file->load_base + rel->r_offset);
  unsigned long reloc_addend = 0;
  unsigned long reloc_sym = ELFW_R_SYM (rel->r_info);

  return do_process_reloc (file, reloc_type, reloc_addr, reloc_addend, reloc_sym);
}

static unsigned long
process_rela (struct VdlFile *file, ElfW(Rela) *rela)
{
  unsigned long reloc_type = ELFW_R_TYPE (rela->r_info);
  unsigned long *reloc_addr = (unsigned long*) (file->load_base + rela->r_offset);
  unsigned long reloc_addend = rela->r_addend;
  unsigned long reloc_sym = ELFW_R_SYM (rela->r_info);

  return do_process_reloc (file, reloc_type, reloc_addr, reloc_addend, reloc_sym);
}

static void
reloc_jmprel (struct VdlFile *file)
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
unsigned long 
vdl_file_reloc_one_jmprel (struct VdlFile *file, 
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


static void
reloc_dtrel (struct VdlFile *file)
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

static void
reloc_dtrela (struct VdlFile *file)
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

static void
do_reloc (struct VdlFile *file, int now)
{
  if (file->reloced)
    {
      return;
    }
  file->reloced = 1;

  reloc_dtrel (file);
  reloc_dtrela (file);
  if (now)
    {
      // perform full PLT relocs _now_
      reloc_jmprel (file);
    }
  else
    {
      machine_lazy_reloc (file);
    }
}

void vdl_file_reloc (struct VdlFileList *files, int now)
{
  struct VdlFileList *sorted = vdl_sort_increasing_depth (files);
  sorted = vdl_file_list_reverse (sorted);
  struct VdlFileList *cur;
  for (cur = sorted; cur != 0; cur = cur->next)
    {
      do_reloc (cur->item, now);
    }
}
