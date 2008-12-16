#ifndef MDL_H
#define MDL_H

#include <stdint.h>
#include <sys/types.h>
#include "alloc.h"

struct MappedFileList
{
  struct MappedFile *dep;
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
  uint32_t context;
  dev_t st_dev;
  ino_t st_ino;
  unsigned long ro_start;
  unsigned long ro_file_offset;
  uint32_t ro_size;
  uint32_t rw_size;
  uint32_t init_called : 1;
  uint32_t fini_called : 1;
  enum LookupType lookup_type;
  struct MappedFileList *local_scope;
  char *interpreter_name;
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
  MDL_LOG_ERR    = (1<<2)
};


struct Mdl
{
  // the following fields are part of the ABI. Don't touch them.
  int version; // always 1
  struct MappedFile *link_map;
  int (*breakpoint)(void);
  enum MdlState state;
  unsigned long interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t logging;
  struct StringList *search_dirs;
  struct MappedFileList *global_scope;
  uint32_t next_context;
  struct Alloc alloc;
};


extern struct Mdl g_mdl;

void mdl_initialize (unsigned long interpreter_load_base);
struct MappedFile *mdl_load_file (const char *filename);
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





#endif /* MDL_H */
