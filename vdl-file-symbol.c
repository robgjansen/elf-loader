#include "vdl-file-symbol.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-file-list.h"
#include <stdint.h>


static uint32_t
vdl_gnu_hash (const char *s)
{
  // Copy/paste from the glibc source code.
  // This function is coming from comp.lang.c and was originally
  // posted by Daniel J Bernstein
  uint32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    {
      h = h * 33 + c;
    }
  return h;
}

static unsigned long
vdl_elf_hash (const char *n)
{
  // Copy/paste from the ELF specification (figure 2-9)
  const unsigned char *name = (const unsigned char *) n;
  unsigned long h = 0, g;
  while (*name)
    {
      h = (h << 4) + *name++;
      if ((g = (h & 0xf0000000)))
	h ^= g >> 24;
      h &= ~g;
    }
  return h;
}

struct FileLookupIterator
{
  const char *name;
  const char *dt_strtab;
  ElfW(Sym) *dt_symtab;
  enum {
    ELF_HASH,
    GNU_HASH,
    ELF_SYM,
    NO_SYM
  } type;
  union {
    struct {
      signed long current;
      ElfW(Word) *chain;
    } elf;
    struct {
      uint32_t current;
      uint32_t *cur_hash;
    } gnu;
  } u;
};

static struct FileLookupIterator 
vdl_file_lookup_begin (const struct VdlFile *file,
		       const char *name, 
		       unsigned long elf_hash,
		       uint32_t gnu_hash)
{
  VDL_LOG_FUNCTION ("name=%s, elf_hash=0x%lx, gnu_hash=0x%x, file=%s", name, elf_hash, gnu_hash, file->filename);
  struct FileLookupIterator i;
  i.name = name;
  // first, gather information needed to look into the hash table
  i.dt_strtab = (const char *) vdl_file_get_dynamic_p (file, DT_STRTAB);
  i.dt_symtab = (ElfW(Sym)*) vdl_file_get_dynamic_p (file, DT_SYMTAB);
  ElfW(Word) *dt_hash = (ElfW(Word)*) vdl_file_get_dynamic_p (file, DT_HASH);
  uint32_t *dt_gnu_hash = (ElfW(Word)*) vdl_file_get_dynamic_p (file, DT_GNU_HASH);

  if (i.dt_strtab == 0 || i.dt_symtab == 0)
    {
      i.type = NO_SYM;
    }
  else if (dt_gnu_hash != 0)
    {
      i.type = NO_SYM; // by default, unless we can find a matching chain
      // read header
      uint32_t nbuckets = dt_gnu_hash[0];
      uint32_t symndx = dt_gnu_hash[1];
      uint32_t maskwords = dt_gnu_hash[2];
      uint32_t shift2 = dt_gnu_hash[3];
      // read other parts of hash table
      ElfW(Word) *bloom = (ElfW(Word)*)(dt_gnu_hash + 4);
      uint32_t *buckets = (uint32_t *)(((unsigned long)bloom) + maskwords * sizeof (ElfW(Word)));
      uint32_t *chains = &buckets[nbuckets];

      // test against the Bloom filter
      uint32_t hashbit1 = gnu_hash % __ELF_NATIVE_CLASS;
      uint32_t hashbit2 = (gnu_hash >> shift2) % __ELF_NATIVE_CLASS;
      ElfW(Word) bitmask = (1<<hashbit1) | (1<<hashbit2);
      ElfW(Word) bitmask_word = bloom[(gnu_hash / __ELF_NATIVE_CLASS) % maskwords];
      if ((bitmask_word & bitmask) == bitmask)
	{
	  // check bucket
	  uint32_t chain = buckets[gnu_hash % nbuckets];
	  if (chain != 0)
	    {
	      // we have the start of the chain !
	      i.type = GNU_HASH;
	      i.u.gnu.current = chain;
	      i.u.gnu.cur_hash = &chains[chain-symndx];
	    }
	}
    }
  else if (dt_hash != 0)
    {
      i.type = ELF_HASH;
      // Then, look into the hash table itself.
      // First entry is number of buckets
      // Second entry is number of chains
      ElfW(Word) nbuckets = dt_hash[0];
      i.u.elf.chain = &dt_hash[2+nbuckets];
      // the code below is tricky: normally, the index of the
      // first entry we want to look at in the hash table is
      // 2+(hash%nbuckets) relative to the dt_hash pointer.
      // what we calculate below is the index in the hash table
      // relative to the chain pointer and the reason we do
      // this is that all other indexes in the hash chain
      // are relative to the chain pointer so, using an index
      // relative to the chain pointer all the time allows us
      // to use the same logic in has_next all the time.
      i.u.elf.current = -(nbuckets-(elf_hash%nbuckets));
    }
  else
    {
      i.type = ELF_SYM;  
    }

  return i;
}

static int
vdl_file_lookup_has_next (const struct FileLookupIterator *i)
{
  switch (i->type) {
  case NO_SYM:
    return 0;
    break;
  case ELF_HASH: {
    unsigned long prev = i->u.elf.current;
    unsigned long current = i->u.elf.chain[i->u.elf.current];
    unsigned long found = 0;
    while (current != 0)
      {
	// The values stored in the hash table are
	// an index in the symbol table.
	if (i->dt_symtab[current].st_name != 0 && 
	    i->dt_symtab[current].st_shndx != SHN_UNDEF)
	  {
	    // the symbol name is an index in the string table
	    if (vdl_utils_strisequal (i->dt_strtab + i->dt_symtab[current].st_name, i->name))
	      {
		found = 1;
		break;
	      }
	  }
	prev = current;
	current = i->u.elf.chain[current];
      }
    // as an optimization, to save us from iterating again
    // in the _next function, we set the current position
    // to the previous entry to find the matching entry 
    // immediately upon our call to _next.
    ((struct FileLookupIterator *)i)->u.elf.current = prev;
    return found;
  } break;
  case GNU_HASH: {
    unsigned long current = i->u.gnu.current;
    uint32_t *cur_hash = i->u.gnu.cur_hash;
    unsigned long found = 0;
    while (1)
      {
	// The values stored in the hash table are
	// an index in the symbol table.
	if (i->dt_symtab[current].st_name != 0 && 
	    i->dt_symtab[current].st_shndx != SHN_UNDEF)
	  {
	    // the symbol name is an index in the string table
	    if (vdl_utils_strisequal (i->dt_strtab + i->dt_symtab[current].st_name, i->name))
	      {
		found = 1;
		break;
	      }
	  }
	if ((*cur_hash & 0x1) == 0x1)
	  {
	    break;
	  }
	cur_hash++;
	current++;
      }
    // as an optimization, to save us from iterating again
    // in the _next function, we set the current position
    // to the previous entry to find the matching entry 
    // immediately upon our call to _next.
    struct FileLookupIterator *i_unconst = (struct FileLookupIterator *)i;
    i_unconst->u.gnu.current = current;
    i_unconst->u.gnu.cur_hash = cur_hash;
    return found;
  } break;
  case ELF_SYM:
    return 0;
    break;
  }
  VDL_LOG_ASSERT (0, "We can't reach here");
  return 0;
}

// return index in dt_symtab
static unsigned long
vdl_file_lookup_next (struct FileLookupIterator *i)
{
  switch (i->type) {
  case NO_SYM:
    VDL_LOG_ASSERT (0, "This is a programming error");
    break;
  case ELF_HASH: {
    VDL_LOG_ASSERT (vdl_file_lookup_has_next (i), "Next called while no data to read");
    // We return the entry immediately following the
    // 'current' index and update the 'current' index
    // to point to the next entry.
    unsigned long next = i->u.elf.chain[i->u.elf.current];
    i->u.elf.current = next;
    return next;
  } break;
  case GNU_HASH:
    VDL_LOG_ASSERT (vdl_file_lookup_has_next (i), "Next called while no data to read");
    unsigned long next = i->u.gnu.current;
    // goto the next entry
    i->u.gnu.current++;
    i->u.gnu.cur_hash++;
    return next;
    break;
  case ELF_SYM:
    return 0;
    break;
  }
  VDL_LOG_ASSERT (0, "We can't reach here");
  return 0;
}

// we have a matching symbol but we have a version
// requirement so, we must check that the matching 
// symbol's version also matches.	      
static int
symbol_version_matches (const struct VdlFile *in,
			const struct VdlFile *from,
			const ElfW(Vernaux) *ver_needed,
			unsigned long index)
{
  if (ver_needed == 0)
    {
      // if we have no version requirement, the first matching symbol is ok.
      return 1;
    }
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (in, DT_STRTAB);
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_file_get_dynamic_p (in, DT_VERSYM);
  ElfW(Verdef) *dt_verdef = (ElfW(Verdef)*)vdl_file_get_dynamic_p (in, DT_VERDEF);
  unsigned long dt_verdefnum = vdl_file_get_dynamic_v (in, DT_VERDEFNUM);
  if (dt_versym == 0)
    {
      // if we have no version definition which could match the requested
      // version, this is not a matching symbol.
      return 0;
    }
  uint16_t ver_index = dt_versym[index];
  if (ver_index == 0)
    {
      // this is a symbol with local scope
      // it's ok if the reference is within the same file.
      return in == from;
    }
  if (ver_index == 1)
    {
      // this is a symbol with global scope. 'base' version definition.
      // XXX: I don't know what this means.
      return 0;
    }
  if (dt_verdef == 0 || dt_verdefnum == 0)
    {
      // if there is no verdef array which could match the requested
      // version, this is not a matching symbol.
      return 0;
    }
  if (ver_index & 0x8000)
    {
      // if the high bit is set, this means that it is a 'hidden' symbol
      // which means that it can't be referenced from outside of its binary.
      if (in != from)
	{
	  // the matching symbol we found is hidden and is located
	  // in a different binary. Not ok.
	  return 0;
	}
    }
  VDL_LOG_DEBUG ("index=%x/%x, %x:%x\n", ver_index, dt_verdefnum,
		 dt_versym, dt_verdef);
  ElfW(Verdef) *cur, *prev;
  for (prev = 0, cur = dt_verdef; cur != prev;
       prev = cur, cur = (ElfW(Verdef)*)(((unsigned long)cur)+cur->vd_next))
    {
      VDL_LOG_ASSERT (cur->vd_version == 1, "version number invalid for Verdef");
      if (cur->vd_ndx == ver_index &&
	  cur->vd_hash == ver_needed->vna_hash)
	{
	  // the hash values of the version names are equal.
	  ElfW(Verdaux) *verdaux = (ElfW(Verdaux)*)(((unsigned long)cur)+cur->vd_aux);
	  const char *from_dt_strtab = (const char *)vdl_file_get_dynamic_p (from, DT_STRTAB);
	  if (vdl_utils_strisequal (dt_strtab + verdaux->vda_name, 
			      from_dt_strtab + ver_needed->vna_name))
	    {
	      // the version names are equal.
	      return 1;
	    }
	}
    }
  // the versions don't match.
  return 0;
}

static int
vdl_file_do_symbol_lookup_scope (struct VdlFile *file,
				 const char *name, 
				 unsigned long elf_hash,
				 uint32_t gnu_hash,
				 const ElfW(Vernaux) *ver_needed,
				 enum LookupFlag flags,
				 struct VdlFileList *scope,
				 struct SymbolMatch *match)
{
  VDL_LOG_FUNCTION ("name=%s, elg_hash=0x%lx, gnu_hash=0x%x, scope=%p", name, elf_hash, gnu_hash, scope);

  // then, iterate scope until we find the requested symbol.
  struct VdlFileList *cur;
  for (cur = scope; cur != 0; cur = cur->next)
    {
      if (flags & LOOKUP_NO_EXEC && 
	  cur->item->is_executable)
	{
	  // this flag specifies that we should not lookup symbols
	  // in the main executable binary. see the definition of LOOKUP_NO_EXEC
	  continue;
	}
      struct FileLookupIterator i = vdl_file_lookup_begin (cur->item, name, elf_hash, gnu_hash);
      while (vdl_file_lookup_has_next (&i))
	{
	  unsigned long index = vdl_file_lookup_next (&i);
	  if (symbol_version_matches (cur->item, file, ver_needed, index))
	    {
	      // We have resolved the symbol
	      if (cur->item != file)
		{
		  // The symbol has been resolved in another binary. Make note of this.
		  file->gc_symbols_resolved_in = vdl_file_list_prepend_one (file->gc_symbols_resolved_in, 
									    cur->item);
		  VDL_LOG_DEBUG ("resolved %s in=%s from=%s\n", name, cur->item->name, file->name);
		}
	      match->file = cur->item;
	      match->symbol = &i.dt_symtab[index];
	      return 1;
	    }
	}
    }
  return 0;
}

int 
vdl_file_symbol_lookup (struct VdlFile *file,
			const char *name, 
			const ElfW(Vernaux) *ver,
			enum LookupFlag flags,
			struct SymbolMatch *match)
{
  // calculate the hash here to avoid calculating 
  // it twice in both calls to symbol_lookup
  unsigned long elf_hash = vdl_elf_hash (name);
  uint32_t gnu_hash = vdl_gnu_hash (name);

  struct VdlFileList *first = 0;
  struct VdlFileList *second = 0;
  switch (file->lookup_type)
    {
    case LOOKUP_LOCAL_GLOBAL:
      first = file->local_scope;
      second = file->context->global_scope;
      break;
    case LOOKUP_GLOBAL_LOCAL:
      first = file->context->global_scope;
      second = file->local_scope;
      break;
    case LOOKUP_GLOBAL_ONLY:
      first = file->context->global_scope;
      second = 0;
      break;
    case LOOKUP_LOCAL_ONLY:
      first = file->local_scope;
      second = 0;
      break;
    }
  int ok = vdl_file_do_symbol_lookup_scope (file, name, elf_hash, gnu_hash, ver,
					    flags, first, match);
  if (!ok)
    {
      ok = vdl_file_do_symbol_lookup_scope (file, name, elf_hash, gnu_hash, ver,
					    flags, second, match);
    }
  return ok;
}
unsigned long 
vdl_file_symbol_lookup_local (const struct VdlFile *file, const char *name)
{
  unsigned long elf_hash = vdl_elf_hash (name);
  uint32_t gnu_hash = vdl_gnu_hash (name);
  struct FileLookupIterator i = vdl_file_lookup_begin (file, name, elf_hash, gnu_hash);
  while (vdl_file_lookup_has_next (&i))
    {
      unsigned long index = vdl_file_lookup_next (&i);
      return file->load_base + i.dt_symtab[index].st_value;
    }
  return 0;
}
