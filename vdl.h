#ifndef MDL_H
#define MDL_H

#include <stdint.h>
#include <sys/types.h>
#include "alloc.h"
#include "system.h"

struct MappedFileList
{
  struct MappedFile *item;
  struct MappedFileList *next;
};

enum LookupType
{
  // indicates that lookups within this object should be performed
  // using the global scope only and that local scope should be ignored.
  LOOKUP_GLOBAL_ONLY,
  LOOKUP_GLOBAL_LOCAL,
  LOOKUP_LOCAL_GLOBAL,
  LOOKUP_LOCAL_ONLY,
};

struct FileInfo
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

struct MappedFile
{
  // The following fields are part of the ABI. Don't change them
  unsigned long load_base;
  // the fullname of this file.
  char *filename;
  // pointer to the PT_DYNAMIC area
  unsigned long dynamic;
  struct MappedFile *next;
  struct MappedFile *prev;

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
  enum LookupType lookup_type;
  struct Context *context;
  struct MappedFileList *local_scope;
  // list of files this file depends upon. 
  // equivalent to the content of DT_NEEDED.
  struct MappedFileList *deps;
};

struct StringList
{
  char *str;
  struct StringList *next;
};
enum MdlState {
  MDL_CONSISTENT,
  MDL_ADD,
  MDL_DELETE
};
enum MdlLog {
  MDL_LOG_FUNC     = (1<<0),
  MDL_LOG_DBG      = (1<<1),
  MDL_LOG_ERR      = (1<<2),
  MDL_LOG_AST      = (1<<3),
  MDL_LOG_SYM_FAIL = (1<<4),
  MDL_LOG_REL      = (1<<5),
  MDL_LOG_SYM_OK   = (1<<6),
  MDL_LOG_PRINT    = (1<<7)
};

struct Context
{
  struct Context *prev;
  struct Context *next;
  struct MappedFileList *global_scope;
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

struct Mdl
{
  // the following fields are part of the gdb/libc ABI. Don't touch them.
  int version; // always 1
  struct MappedFile *link_map;
  int (*breakpoint)(void);
  enum MdlState state;
  unsigned long interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t logging;
  // The list of directories to search for binaries
  // in DT_NEEDED entries.
  struct StringList *search_dirs;
  // The data structure used by the memory allocator
  // all heap memory allocations through vdl_alloc
  // and vdl_free end up here.
  struct Alloc alloc;
  uint32_t bind_now : 1;
  struct Context *contexts;
  unsigned long tls_gen;
  unsigned long tls_static_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
};


extern struct Mdl g_vdl;

// control setup of core data structures
void vdl_initialize (unsigned long interpreter_load_base);
struct Context *vdl_context_new (int argc, const char **argv, const char **envp);
struct MappedFile *vdl_file_new (unsigned long load_base,
				 const struct FileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct Context *context);

void vdl_linkmap_print (void);

// expect a ':' separated list
void vdl_set_logging (const char *debug_str);

// allocate/free memory
void *vdl_malloc (size_t size);
void vdl_free (void *buffer, size_t size);
#define vdl_new(type) \
  (type *) vdl_malloc (sizeof (type))
#define vdl_delete(v) \
  vdl_free (v, sizeof(*v))

// string manipulation functions
int vdl_strisequal (const char *a, const char *b);
int vdl_strlen (const char *str);
char *vdl_strdup (const char *str);
void vdl_memcpy (void *dst, const void *src, size_t len);
void vdl_memset(void *s, int c, size_t n);
char *vdl_strconcat (const char *str, ...);
const char *vdl_getenv (const char **envp, const char *value);

// convenience function
int vdl_exists (const char *filename);

// manipulate string lists.
struct StringList *vdl_strsplit (const char *value, char separator);
void vdl_str_list_free (struct StringList *list);
struct StringList *vdl_str_list_reverse (struct StringList *list);
struct StringList * vdl_str_list_append (struct StringList *start, struct StringList *end);

// logging
void vdl_log_printf (enum MdlLog log, const char *str, ...);
#define MDL_LOG_FUNCTION(str,...)					\
  vdl_log_printf (MDL_LOG_FUNC, "%s:%d, %s (" str ")\n",		\
		  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MDL_LOG_DEBUG(str,...) \
  vdl_log_printf (MDL_LOG_DBG, str, __VA_ARGS__)
#define MDL_LOG_ERROR(str,...) \
  vdl_log_printf (MDL_LOG_ERR, str, __VA_ARGS__)
#define MDL_LOG_SYMBOL_FAIL(symbol,file)				\
  vdl_log_printf (MDL_LOG_SYM_FAIL, "Could not resolve symbol=%s, file=%s\n", \
		  symbol, file->filename)
#define MDL_LOG_SYMBOL_OK(symbol_name,from,match)			\
  vdl_log_printf (MDL_LOG_SYM_OK, "Resolved symbol=%s, from file=\"%s\", in file=\"%s\":0x%x\n", \
		  symbol_name, from->filename, match->file->filename,	\
		  match->file->load_base + match->symbol->st_value)
#define MDL_LOG_RELOC(rel)					      \
  vdl_log_printf (MDL_LOG_REL, "Unhandled reloc type=0x%x at=0x%x\n", \
		  ELFW_R_TYPE (rel->r_info), rel->r_offset)
#define MDL_ASSERT(predicate,str)		 \
  if (!(predicate))				 \
    {						 \
      vdl_log_printf (MDL_LOG_AST, "%s\n", str); \
      system_exit (-1);				 \
    }



// manipulate lists of files
void vdl_file_list_free (struct MappedFileList *list);
struct MappedFileList *vdl_file_list_copy (struct MappedFileList *list);
struct MappedFileList *vdl_file_list_append_one (struct MappedFileList *list, 
						 struct MappedFile *item);
struct MappedFileList *vdl_file_list_append (struct MappedFileList *start, 
					     struct MappedFileList *end);
void vdl_file_list_unicize (struct MappedFileList *list);
unsigned long vdl_align_down (unsigned long v, unsigned long align);
unsigned long vdl_align_up (unsigned long v, unsigned long align);



#endif /* MDL_H */
