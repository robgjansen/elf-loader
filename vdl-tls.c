#include "vdl.h"
#include "vdl-tls.h"
#include "vdl-config.h"
#include "vdl-log.h"
#include "vdl-utils.h"
#include "vdl-sort.h"
#include "vdl-file-list.h"
#include "machine.h"

static unsigned long
allocate_tls_index (void)
{
  // This is the slowest but simplest implementation possible
  // For each possible module index, we try to find a module
  // which has been already assigned that module index.
  // If it has been already assigned, we try another one, otherwise,
  // we return it.
  unsigned long i;
  unsigned long ul_max = 0;
  ul_max = ~ul_max;
  for (i = 1; i < ul_max; i++)
    {
      struct VdlFile *cur;
      for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
	{
	  if (cur->has_tls && cur->tls_index == i)
	    {
	      break;
	    }
	}
      if (cur == 0)
	{
	  return i;
	}
    }
  VDL_LOG_ASSERT (0, "All tls module indexes are used up ? impossible !");
  return 0; // quiet compiler
}

static void
file_initialize (struct VdlFile *file)
{
  if (file->tls_initialized)
    {
      return;
    }
  file->tls_initialized = 1;

  ElfW(Ehdr) *header = (ElfW(Ehdr) *)file->ro_map.mem_start_align;
  ElfW(Phdr) *phdr = (ElfW(Phdr) *) (file->ro_map.mem_start_align + header->e_phoff);
  ElfW(Phdr) *pt_tls = vdl_utils_search_phdr (phdr, header->e_phnum, PT_TLS);
  unsigned long dt_flags = vdl_file_get_dynamic_v (file, DT_FLAGS);
  if (pt_tls == 0)
    {
      file->has_tls = 0;
      return;
    }
  file->has_tls = 1;
  file->tls_tmpl_start = file->load_base + pt_tls->p_vaddr;
  file->tls_tmpl_size = pt_tls->p_filesz;
  file->tls_init_zero_size = pt_tls->p_memsz - pt_tls->p_filesz;
  file->tls_align = pt_tls->p_align;
  file->tls_index = allocate_tls_index ();
  file->tls_is_static = (dt_flags & DF_STATIC_TLS)?1:0;
  file->tls_tmpl_gen = g_vdl.tls_gen;
  g_vdl.tls_gen++;
  g_vdl.tls_n_dtv++;
}

void
vdl_tls_file_initialize (struct VdlFileList *files)
{
  // The only thing we need to make sure here is that the executable 
  // gets assigned tls module id 1 if needed.
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      if (cur->item->is_executable)
	{
	  file_initialize (cur->item);
	  break;
	}
    }
  for (cur = files; cur != 0; cur = cur->next)
    {
      if (!cur->item->is_executable)
	{
	  file_initialize (cur->item);
	}
    }
}

static void 
file_deinitialize (struct VdlFile *file)
{
  if (!file->tls_initialized)
    {
      return;
    }
  file->tls_initialized = 0;

  if (file->has_tls)
    {
      g_vdl.tls_gen++;
      g_vdl.tls_n_dtv--;
    }
}

void vdl_tls_file_deinitialize (struct VdlFileList *files)
{
  // the deinitialization order here does not matter at all.
  struct VdlFileList *cur;
  for (cur = files; cur != 0; cur = cur->next)
    {
      file_deinitialize (cur->item);
    }  
}

bool
vdl_tls_file_list_has_static (struct VdlFileList *list)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (cur->item->has_tls && cur->item->tls_is_static)
	{
	  return true;
	}
    }
  return false;
}


void
vdl_tls_initialize (void)
{
  g_vdl.tls_gen = 1;
  // We gather tls information for each module. 
  {
    struct VdlFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	file_initialize (cur);
      }
  }
  // Then, we calculate the size of the memory needed for the 
  // static and local tls model. We also initialize correctly
  // the tls_offset field to be able to perform relocations
  // next (the TLS relocations need the tls_offset field).
  {
    unsigned long tcb_size = 0;
    unsigned long n_dtv = 0;
    unsigned long max_align = 1;
    struct VdlFile *cur;
    for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
      {
	if (cur->has_tls)
	  {
	    if (cur->tls_is_static)
	      {
		tcb_size += cur->tls_tmpl_size + cur->tls_init_zero_size;
		tcb_size = vdl_utils_align_up (tcb_size, cur->tls_align);
		cur->tls_offset = - tcb_size;
		if (cur->tls_align > max_align)
		  {
		    max_align = cur->tls_align;
		  }
	      }
	    n_dtv++;
	  }
      }
    g_vdl.tls_static_size = vdl_utils_align_up (tcb_size, max_align);
    g_vdl.tls_static_align = max_align;
  }
}

unsigned long
vdl_tls_tcb_allocate (void)
{
  // we allocate continuous memory for the set of tls blocks + libpthread TCB
  unsigned long tcb_size = g_vdl.tls_static_size;
  unsigned long total_size = tcb_size + CONFIG_TCB_SIZE; // specific to variant II
  unsigned long buffer = (unsigned long) vdl_utils_malloc (total_size);
  vdl_utils_memset ((void*)buffer, 0, total_size);
  unsigned long tcb = buffer + tcb_size;
  // complete setup of TCB
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_TCB_OFFSET), &tcb, sizeof (tcb));
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_SELF_OFFSET), &tcb, sizeof (tcb));
  return tcb;
}

void
vdl_tls_tcb_initialize (unsigned long tcb, unsigned long sysinfo)
{
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), &sysinfo, sizeof (sysinfo));
}

// This dtv structure needs to be compatible with the one used by the 
// glibc loader. Although it's supposed to be opaque to the glibc or 
// libpthread, it's not. nptl_db reads it to lookup tls variables (it
// reads dtv[i].value where i >= 1 to find out the address of a target
// tls block) and libpthread reads dtv[-1].value to find out the size
// of the dtv array and be able to memset it to zeros.
// The only leeway we have is in the glibc static field which we reuse
// to store a per-dtvi generation counter.
struct dtv_t
{
  unsigned long value;
  unsigned long gen;
};

void
vdl_tls_dtv_allocate (unsigned long tcb)
{
  // allocate a dtv for the set of tls blocks needed now
  struct dtv_t *dtv = vdl_utils_malloc ((2+g_vdl.tls_n_dtv) * sizeof (struct dtv_t));
  dtv[0].value = g_vdl.tls_n_dtv;
  dtv[0].gen = 0;
  dtv++;
  dtv[0].value = 0;
  dtv[0].gen = g_vdl.tls_gen;
  vdl_utils_memcpy ((void*)(tcb+CONFIG_TCB_DTV_OFFSET), &dtv, sizeof (dtv));
}

void
vdl_tls_dtv_initialize (unsigned long tcb)
{
  struct dtv_t *dtv;
  vdl_utils_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  // allocate a dtv for the set of tls blocks needed now

  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->has_tls)
	{
	  // setup the dtv to point to the tls block
	  if (cur->tls_is_static)
	    {
	      signed long dtvi = tcb + cur->tls_offset;
	      dtv[cur->tls_index].value = dtvi;
	      // copy the template in the module tls block
	      vdl_utils_memcpy ((void*)dtvi, (void*)cur->tls_tmpl_start, cur->tls_tmpl_size);
	      vdl_utils_memset ((void*)(dtvi + cur->tls_tmpl_size), 0, cur->tls_init_zero_size);
	    }
	  else
	    {
	      dtv[cur->tls_index].value = 0; // unallocated
	    }
	  dtv[cur->tls_index].gen = cur->tls_tmpl_gen;
	}
    }
  // initialize its generation counter
  dtv[0].gen = g_vdl.tls_gen;
}

static struct VdlFile *
find_file_by_module (unsigned long module)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      if (cur->has_tls && cur->tls_index == module)
	{
	  return cur;
	}
    }
  return 0;
}

void
vdl_tls_dtv_deallocate (unsigned long tcb)
{
  struct dtv_t *dtv;
  vdl_utils_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));

  unsigned long dtv_size = dtv[-1].value;
  unsigned long module;
  for (module = 1; module <= dtv_size; module++)
    {
      if (dtv[module].value == 0)
	{
	  // this was an unallocated entry
	  continue;
	}
      struct VdlFile *file = find_file_by_module (module);
      if (!file->tls_is_static)
	{
	  // this was not a static entry
	  unsigned long *dtvi = (unsigned long *)dtv[module].value;
	  vdl_utils_free (dtvi, dtvi[-1]);
	}
    }
  vdl_utils_free (&dtv[-1], (dtv[-1].value+2)*sizeof(struct dtv_t));
}

void
vdl_tls_tcb_deallocate (unsigned long tcb)
{
  unsigned long start = tcb - g_vdl.tls_static_size;
  vdl_utils_free ((void*)start, g_vdl.tls_static_size + CONFIG_TCB_SIZE);
}
static struct dtv_t *
get_current_dtv (void)
{
  // get the thread pointer for the current calling thread
  unsigned long tp = machine_thread_pointer_get ();
  // extract the dtv from it
  struct dtv_t *dtv;
  vdl_utils_memcpy (&dtv, (void*)(tp+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  return dtv;
}
unsigned long vdl_tls_get_addr_fast (unsigned long module, unsigned long offset)
{
  struct dtv_t *dtv = get_current_dtv ();
  if (dtv[0].gen == g_vdl.tls_gen && dtv[module].value != 0)
    {
      // our dtv is really uptodate _and_ the requested module block
      // has been already initialized.
      return dtv[module].value + offset;
    }
  // either we need to update the dtv or we need to initialize
  // the dtv entry to point to the requested module block
  return 0;
}
static void
update_dtv (void)
{
  unsigned long tp = machine_thread_pointer_get ();
  struct dtv_t *dtv = get_current_dtv ();
  unsigned long dtv_size = dtv[-1].value;
  // first, check its size
  if (g_vdl.tls_n_dtv <= dtv_size)
    {
      // ok, our current dtv is big enough. We just need
      // to update its content
      unsigned long module;
      for (module = 1; module <= dtv_size; module++)
	{
	  struct VdlFile *file = find_file_by_module (module);
	  if (file == 0)
	    {
	      // the module has been unloaded so, well,
	      // nothing to do here, just skip this empty entry
	      continue;
	    }
	  if (dtv[module].gen == file->tls_tmpl_gen)
	    {
	      // this entry is uptodate. skip it
	      continue;
	    }
	  VDL_LOG_ASSERT (!file->tls_is_static, "tls modules with static tls blocks should "
			  "always be uptodate");
	  if (dtv[module].value == 0)
	    {
	      // this is just an un-initialized entry so, we leave it alone
	      continue;
	    }
	  // Now, we know that this entry was initialized at some point
	  // but it's not uptodate anymore. This can happen only if
	  // the user has unmapped a module with tls capability, 
	  // and reloaded a new module with tls capability and the
	  // new module has been allocated the same tls module index
	  // as the old module. All we have to do here is make sure that this
	  // entry gets initialized when it's needed
	  dtv[module].value = 0;
	}
      // now that the dtv is updated, update the generation
      dtv[0].gen = g_vdl.tls_gen;
      return;
    }

  // the size of the new dtv is bigger than the 
  // current dtv. We need a newly-sized dtv
  vdl_tls_dtv_allocate (tp);
  struct dtv_t *new_dtv = get_current_dtv ();
  unsigned long new_dtv_size = new_dtv[-1].value;
  unsigned long module;
  // first, initialize the common area between
  // the old and the new dtv. also, clear
  // the old dtv
  for (module = 1; module <= dtv_size; module++)
    {
      struct VdlFile *file = find_file_by_module (module);
      if (file == 0)
	{
	  // the module has been unloaded so, well,
	  // nothing to do here. for clarity, we initialize
	  // these entries, but it's not needed, really.
	  new_dtv[module].value = 0;
	  new_dtv[module].gen = 0;
	  continue;
	}
      if (dtv[module].gen == file->tls_tmpl_gen)
	{
	  // copy uptodate entries
	  new_dtv[module] = dtv[module];	  
	  continue;
	}
      if (dtv[module].value == 0)
	{
	  // an uninitialized entry. nothing to do. for clarity, 
	  // initialize new dtv too.
	  new_dtv[module].value = 0;
	  new_dtv[module].gen = 0;
	  continue;
	}
      // these are entries which were initialized at some point in the past 
      // but which are not uptodate anymore. i.e., entries which represent 
      // an old unloaded module with a newly-loaded module, both with the 
      // same tls module index.
      // first, free the old block (the size of the block is located
      // just before the start of the block)
      unsigned long * dtvi = (unsigned long *)dtv[module].value;
      vdl_utils_free (dtvi, dtvi[-1]);
      // then, make sure the new block will be initialized later
      new_dtv[module].value = 0;
      new_dtv[module].gen = 0;
    }
  // then, initialize the new area in the new dtv
  for (module = dtv_size+1; module <= new_dtv_size; module++)
    {
      new_dtv[module].value = 0;
      new_dtv[module].gen = 0;
      struct VdlFile *file = find_file_by_module (module);
      if (file == 0)
	{
	  // the module has been loaded and then unloaded before
	  // we updated our dtv so, well,
	  // nothing to do here, just skip this empty entry
	  continue;
	}
      VDL_LOG_ASSERT (!file->tls_is_static, "tls modules with static tls blocks should never"
		      "be initialized here");
    }
  // finally, clear the old dtv
  vdl_utils_free (&dtv[-1], (dtv[-1].value+2)*sizeof(struct dtv_t));
}
unsigned long vdl_tls_get_addr_slow (unsigned long module, unsigned long offset)
{
  struct dtv_t *dtv = get_current_dtv ();
  if (dtv[0].gen == g_vdl.tls_gen && dtv[module].value != 0)
    {
      // our dtv is really uptodate _and_ the requested module block
      // has been already initialized.
      return dtv[module].value + offset;
    }
  if (dtv[0].gen == g_vdl.tls_gen && dtv[module].value == 0)
    {
      // the dtv is uptodate but the requested module block 
      // has not been initialized already
      struct VdlFile *file = find_file_by_module (module);
      // first, allocate a new tls block for this module
      unsigned long dtvi_size = sizeof(unsigned long) + file->tls_tmpl_size + file->tls_init_zero_size;
      unsigned long *dtvi = vdl_utils_malloc (dtvi_size);
      dtvi[0] = dtvi_size;
      dtvi++;
      // copy the template in the module tls block
      vdl_utils_memcpy (dtvi, (void*)file->tls_tmpl_start, file->tls_tmpl_size);
      vdl_utils_memset ((void*)(((unsigned long)dtvi) + file->tls_tmpl_size), 
			0, file->tls_init_zero_size);
      // finally, update the dtv
      dtv[module].value = (unsigned long)dtvi;
      dtv[module].gen = file->tls_tmpl_gen;
      // and return the requested value
      return dtv[module].value + offset;
    }

  // we know for sure that the dtv is _not_ uptodate now
  update_dtv ();

  // now that the dtv is supposed to be uptodate, attempt to make
  // the request again
  return vdl_tls_get_addr_slow (module, offset);
}
