#ifndef VDL_H
#define VDL_H

#include <stdint.h>
#include <sys/types.h>
#include <elf.h>
#include <link.h>
#include "alloc.h"
#include "system.h"

#if __ELF_NATIVE_CLASS == 32
#define ELFW_R_SYM ELF32_R_SYM
#define ELFW_R_TYPE ELF32_R_TYPE
#define ELFW_ST_BIND(val) ELF32_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF32_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF32_ST_INFO(bind,type)
#else
#define ELFW_R_SYM ELF64_R_SYM
#define ELFW_R_TYPE ELF64_R_TYPE
#define ELFW_ST_BIND(val) ELF64_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF64_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF64_ST_INFO(bind,type)
#endif


struct VdlFileList
{
  struct VdlFile *item;
  struct VdlFileList *next;
};

enum VdlLookupType
{
  // indicates that lookups within this object should be performed
  // using the global scope only and that local scope should be ignored.
  LOOKUP_GLOBAL_ONLY,
  LOOKUP_GLOBAL_LOCAL,
  LOOKUP_LOCAL_GLOBAL,
  LOOKUP_LOCAL_ONLY,
};

struct VdlFileInfo
{
  // vaddr of DYNAMIC program header
  unsigned long dynamic;

  unsigned long ro_start;
  unsigned long ro_size;
  unsigned long rw_start;
  unsigned long rw_size;
  unsigned long zero_start;
  unsigned long zero_size;
  unsigned long ro_file_offset;
  unsigned long rw_file_offset;
  unsigned long memset_zero_start;
  unsigned long memset_zero_size;
};

enum {
  VDL_GC_BLACK = 0,
  VDL_GC_GREY = 1,
  VDL_GC_WHITE = 2
};

struct VdlFile
{
  // The following fields are part of the ABI. Don't change them
  unsigned long load_base;
  // the fullname of this file.
  char *filename;
  // pointer to the PT_DYNAMIC area
  unsigned long dynamic;
  struct VdlFile *next;
  struct VdlFile *prev;

  // The following fields are not part of the ABI
  // This count indicates how many users hold a reference
  // to this file either because the file has been dlopened
  // (the dlopen increases the ref count), or because this
  // file is a dependency of another file, or because this
  // file was loaded during the loader initialization.
  uint32_t count;
  char *name;
  dev_t st_dev;
  ino_t st_ino;
  unsigned long ro_start;
  unsigned long ro_size;
  unsigned long rw_start;
  unsigned long rw_size;
  unsigned long zero_start;
  unsigned long zero_size;
  // indicates if the deps field has been initialized correctly
  uint32_t deps_initialized : 1;
  // indicates if the has_tls field has been initialized correctly
  uint32_t tls_initialized : 1;
  // indicates if the ELF initializers of this file
  // have been called.
  uint32_t init_called : 1;
  // indicates if the ELF finalizers of this file
  // have been called.
  uint32_t fini_called : 1;
  // indicates if this file has been relocated
  uint32_t reloced : 1;
  // indicates if we patched this file for some
  // nastly glibc-isms.
  uint32_t patched : 1;
  // indicates if this file has a TLS program entry
  // If so, all tls_-prefixed variables are valid.
  uint32_t has_tls : 1;
  // indicates if this represents the main executable.
  uint32_t is_executable : 1;
  uint32_t gc_color : 2;
  // the list of objects in which we resolved a symbol 
  // from a GOT/PLT relocation. This field is used
  // during garbage collection from vdl_gc to detect
  // the set of references an object holds to another one
  // and thus avoid unloading an object which is held as a
  // reference by another object.
  struct VdlFileList *gc_symbols_resolved_in;
  // start of TLS block template
  unsigned long tls_tmpl_start;
  // size of TLS block template
  unsigned long tls_tmpl_size;
  // size of TLS block zero area, located
  // right after the area initialized with the
  // TLS block template
  unsigned long tls_init_zero_size;
  // alignment requirements for the TLS block area
  unsigned long tls_align;
  // TLS module index associated to this file
  // this is the index in each thread's DTV
  unsigned long tls_index;
  // offset from thread pointer to this module
  // this field is valid only for modules which
  // are loaded at startup.
  signed long tls_offset;
  enum VdlLookupType lookup_type;
  struct VdlContext *context;
  struct VdlFileList *local_scope;
  // list of files this file depends upon. 
  // equivalent to the content of DT_NEEDED.
  struct VdlFileList *deps;
  uint32_t gc_depth;
};

struct VdlStringList
{
  char *str;
  struct VdlStringList *next;
};
enum VdlState {
  VDL_CONSISTENT,
  VDL_ADD,
  VDL_DELETE
};

struct VdlContext
{
  struct VdlContext *prev;
  struct VdlContext *next;
  struct VdlFileList *global_scope;
  // return the symbol to lookup instead of the input symbol
  const char *(*remap_symbol) (const char *name);
  // return the library to lookup instead of the input library
  const char *(*remap_lib) (const char *name);
  // These variables are used by all .init functions
  // _some_ libc .init functions make use of these
  // 3 arguments so, even though no one else uses them, 
  // we have to pass them around.
  // The arrays below are private copies exclusively used
  // by the loader.
  int argc;
  char **argv;
  char **envp;  
};

struct Vdl
{
  // the following fields are part of the gdb/libc ABI. Don't touch them.
  int version; // always 1
  struct VdlFile *link_map;
  int (*breakpoint)(void);
  enum VdlState state;
  unsigned long interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t logging;
  // The list of directories to search for binaries
  // in DT_NEEDED entries.
  struct VdlStringList *search_dirs;
  // The data structure used by the memory allocator
  // all heap memory allocations through vdl_alloc
  // and vdl_utils_free end up here.
  struct Alloc alloc;
  uint32_t bind_now : 1;
  struct VdlContext *contexts;
  unsigned long tls_gen;
  unsigned long tls_static_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
};


extern struct Vdl g_vdl;

struct VdlContext *vdl_context_new (int argc, const char **argv, const char **envp);
void vdl_context_delete (struct VdlContext *context);
struct VdlFile *vdl_file_new (unsigned long load_base,
				 const struct VdlFileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct VdlContext *context);
void vdl_file_delete (struct VdlFile *file);
struct VdlFile *vdl_file_map_single (struct VdlContext *context, 
				    const char *filename, 
				    const char *name);
int vdl_file_map_deps (struct VdlFile *item);
struct VdlFileList *vdl_file_gather_all_deps_breadth_first (struct VdlFile *file);
void vdl_file_call_init (struct VdlFile *file);
void vdl_file_list_call_fini (struct VdlFileList *list);
unsigned long vdl_file_get_entry_point (struct VdlFile *file);
void vdl_file_reloc (struct VdlFile *file);

struct SymbolMatch
{
  const struct VdlFile *file;
  const ElfW(Sym) *symbol;
};
enum LookupFlag {
  // indicates whether the symbol lookup is allowed to 
  // find a matching symbol in the main binary. This is
  // typically used to perform the lookup associated
  // with a R_*_COPY relocation.
  LOOKUP_NO_EXEC = 1
};
int vdl_file_symbol_lookup (struct VdlFile *file,
			    const char *name, 
			    const ElfW(Vernaux) *ver,
			    enum LookupFlag flags,
			    struct SymbolMatch *match);
struct VdlFile *vdl_file_new_main (unsigned long phnum,
				   ElfW(Phdr)*phdr,
				   int argc, 
				   const char **argv, 
				   const char **envp);
unsigned long vdl_file_symbol_lookup_local (const struct VdlFile *file, const char *name);

void vdl_file_tls (struct VdlFile *file);
ElfW(Dyn) *vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag);

char *vdl_search_filename (const char *name);
int vdl_get_file_info (uint32_t phnum,
		       ElfW(Phdr) *phdr,
		       struct VdlFileInfo *info);
ElfW(Dyn) *vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag);
unsigned long vdl_file_get_dynamic_v (const struct VdlFile *file, unsigned long tag);
unsigned long vdl_file_get_dynamic_p (const struct VdlFile *file, unsigned long tag);
void vdl_file_call_fini (struct VdlFileList *list);
#endif /* VDL_H */
