#ifndef VDL_H
#define VDL_H

#include <stdint.h>
#include <sys/types.h>
#include <elf.h>
#include <link.h>
#include "alloc.h"
#include "system.h"

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
enum VdlLog {
  VDL_LOG_FUNC     = (1<<0),
  VDL_LOG_DBG      = (1<<1),
  VDL_LOG_ERR      = (1<<2),
  VDL_LOG_AST      = (1<<3),
  VDL_LOG_SYM_FAIL = (1<<4),
  VDL_LOG_REL      = (1<<5),
  VDL_LOG_SYM_OK   = (1<<6),
  VDL_LOG_PRINT    = (1<<7)
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
  // and vdl_free end up here.
  struct Alloc alloc;
  uint32_t bind_now : 1;
  struct VdlContext *contexts;
  unsigned long tls_gen;
  unsigned long tls_static_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
};


extern struct Vdl g_vdl;

// control setup of core data structures
void vdl_initialize (unsigned long interpreter_load_base);
struct VdlContext *vdl_context_new (int argc, const char **argv, const char **envp);
struct VdlFile *vdl_file_new (unsigned long load_base,
				 const struct VdlFileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct VdlContext *context);
void vdl_file_ref (struct VdlFile *file);
void vdl_file_unref (struct VdlFile *file);

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

ElfW(Phdr) *vdl_elf_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);
char *vdl_elf_search_file (const char *name);
struct VdlFile *vdl_elf_map_single (struct VdlContext *context, 
				       const char *filename, 
				       const char *name);
int vdl_elf_map_deps (struct VdlFile *item);
int vdl_elf_file_get_info (uint32_t phnum,
			   ElfW(Phdr) *phdr,
			   struct VdlFileInfo *info);
struct VdlFileList *vdl_elf_gather_all_deps_breadth_first (struct VdlFile *file);
unsigned long vdl_elf_hash (const char *n);
void vdl_elf_call_init (struct VdlFile *file);
unsigned long vdl_elf_get_entry_point (struct VdlFile *file);
void vdl_elf_reloc (struct VdlFile *file);
ElfW(Dyn) *vdl_elf_file_get_dynamic (const struct VdlFile *file, unsigned long tag);
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
int vdl_elf_symbol_lookup (const char *name, 
			   const struct VdlFile *file,
			   const ElfW(Vernaux) *ver,
			   enum LookupFlag flags,
			   struct SymbolMatch *match);
struct VdlFile *vdl_elf_main_file_new (unsigned long phnum,
					  ElfW(Phdr)*phdr,
					  int argc, 
					  const char **argv, 
					  const char **envp);
unsigned long vdl_elf_symbol_lookup_local (const char *name, const struct VdlFile *file);

void vdl_elf_tls (struct VdlFile *file);

#endif /* VDL_H */
