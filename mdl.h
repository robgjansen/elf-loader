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
  // vaddr of INTERPRETER program header
  unsigned long interpreter_name;

  //      | <-- ro_size --> |  <--  rw_size  -->  | <-- zero_size --> |
  //      |                 |          | <-zero-> |                   |
  //     /                            /
  //    /                            /
  //  ro_start                    zero_start
  unsigned long ro_start;
  unsigned long ro_size;
  unsigned long rw_size;
  unsigned long zero_start;
  unsigned long zero_size;
  // the ro mapping starts here in the file
  unsigned long ro_file_offset;
};

struct MappedFile
{
  // The following fields are part of the ABI. Don't change them
  unsigned long load_base;
  char *filename;
  unsigned long dynamic;
  struct MappedFile *next;
  struct MappedFile *prev;

  // The following fields are not part of the ABI
  uint32_t count;
  char *name;
  dev_t st_dev;
  ino_t st_ino;
  unsigned long ro_start;
  unsigned long ro_file_offset;
  unsigned long ro_size;
  unsigned long rw_size;
  unsigned long zero_size;
  uint32_t scope_set : 1;
  uint32_t init_called : 1;
  uint32_t fini_called : 1;
  uint32_t reloced : 1;
  enum LookupType lookup_type;
  struct Context *context;
  struct MappedFileList *local_scope;
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
  MDL_LOG_FUNC   = (1<<0),
  MDL_LOG_DBG    = (1<<1),
  MDL_LOG_ERR    = (1<<2),
  MDL_LOG_AST    = (1<<3)
};

struct Context
{
  struct Context *prev;
  struct Context *next;
  struct MappedFileList *global_scope;
  // These variables are used by all .init functions
  // I have never seen an .init function which makes use
  // of these 3 arguments (they all take zero arguments)
  // but the libc loader does pass them around so, for 
  // compatibility, we do the same.
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
  // all heap memory allocations through mdl_alloc
  // and mdl_free end up here.
  struct Alloc alloc;
  uint32_t bind_now : 1;
  struct Context *contexts;
};


extern struct Mdl g_mdl;

// control setup of core data structures
void mdl_initialize (unsigned long interpreter_load_base);
struct Context *mdl_context_new (int argc, const char **argv, const char **envp);
struct MappedFile *mdl_file_new (unsigned long load_base,
				 const struct FileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct Context *context);

// expect a ':' separated list
void mdl_set_logging (const char *debug_str);

// allocate/free memory
void *mdl_malloc (size_t size);
void mdl_free (void *buffer, size_t size);
#define mdl_new(type) \
  (type *) mdl_malloc (sizeof (type))
#define mdl_delete(v) \
  mdl_free (v, sizeof(*v))

// string manipulation functions
int mdl_strisequal (const char *a, const char *b);
int mdl_strlen (const char *str);
char *mdl_strdup (const char *str);
void mdl_memcpy (void *dst, const void *src, size_t len);
void mdl_memset(void *s, int c, size_t n);
char *mdl_strconcat (const char *str, ...);
const char *mdl_getenv (const char **envp, const char *value);

// convenience function
int mdl_exists (const char *filename);

// manipulate string lists.
struct StringList *mdl_strsplit (const char *value, char separator);
void mdl_str_list_free (struct StringList *list);
struct StringList *mdl_str_list_reverse (struct StringList *list);
struct StringList * mdl_str_list_append (struct StringList *start, struct StringList *end);

// logging
void mdl_log_printf (enum MdlLog log, const char *str, ...);
#define MDL_LOG_FUNCTION(str,...)					\
  mdl_log_printf (MDL_LOG_FUNC, "%s:%d, %s (" str ")\n",		\
		  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define MDL_LOG_DEBUG(str,...) \
  mdl_log_printf (MDL_LOG_DBG, str, __VA_ARGS__);
#define MDL_LOG_ERROR(str,...) \
  mdl_log_printf (MDL_LOG_ERR, str, __VA_ARGS__);
#define MDL_ASSERT(predicate,str)		 \
  if (!(predicate))				 \
    {						 \
      mdl_log_printf (MDL_LOG_AST, "%s\n", str); \
      system_exit (-1);				 \
    }



// manipulate lists of files
void mdl_file_list_free (struct MappedFileList *list);
struct MappedFileList *mdl_file_list_copy (struct MappedFileList *list);
struct MappedFileList *mdl_file_list_append_one (struct MappedFileList *list, 
						 struct MappedFile *item);
struct MappedFileList *mdl_file_list_append (struct MappedFileList *start, 
					     struct MappedFileList *end);
void mdl_file_list_unicize (struct MappedFileList *list);



#endif /* MDL_H */
