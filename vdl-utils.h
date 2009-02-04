#ifndef VDL_UTILS_H
#define VDL_UTILS_H

#include <sys/types.h>
#include "vdl.h"

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
struct VdlStringList *vdl_strsplit (const char *value, char separator);
void vdl_str_list_free (struct VdlStringList *list);
struct VdlStringList *vdl_str_list_reverse (struct VdlStringList *list);
struct VdlStringList * vdl_str_list_append (struct VdlStringList *start, struct VdlStringList *end);

// logging
void vdl_log_printf (enum VdlLog log, const char *str, ...);
#define VDL_LOG_FUNCTION(str,...)					\
  vdl_log_printf (VDL_LOG_FUNC, "%s:%d, %s (" str ")\n",		\
		  __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define VDL_LOG_DEBUG(str,...) \
  vdl_log_printf (VDL_LOG_DBG, str, __VA_ARGS__)
#define VDL_LOG_ERROR(str,...) \
  vdl_log_printf (VDL_LOG_ERR, str, __VA_ARGS__)
#define VDL_LOG_SYMBOL_FAIL(symbol,file)				\
  vdl_log_printf (VDL_LOG_SYM_FAIL, "Could not resolve symbol=%s, file=%s\n", \
		  symbol, file->filename)
#define VDL_LOG_SYMBOL_OK(symbol_name,from,match)			\
  vdl_log_printf (VDL_LOG_SYM_OK, "Resolved symbol=%s, from file=\"%s\", in file=\"%s\":0x%x\n", \
		  symbol_name, from->filename, match->file->filename,	\
		  match->file->load_base + match->symbol->st_value)
#define VDL_LOG_RELOC(rel)					      \
  vdl_log_printf (VDL_LOG_REL, "Unhandled reloc type=0x%x at=0x%x\n", \
		  ELFW_R_TYPE (rel->r_info), rel->r_offset)
#define VDL_ASSERT(predicate,str)		 \
  if (!(predicate))				 \
    {						 \
      vdl_log_printf (VDL_LOG_AST, "%s\n", str); \
      system_exit (-1);				 \
    }



// manipulate lists of files
void vdl_file_list_free (struct VdlFileList *list);
struct VdlFileList *vdl_file_list_copy (struct VdlFileList *list);
struct VdlFileList *vdl_file_list_append_one (struct VdlFileList *list, 
						 struct VdlFile *item);
struct VdlFileList *vdl_file_list_append (struct VdlFileList *start, 
					     struct VdlFileList *end);
void vdl_file_list_unicize (struct VdlFileList *list);
unsigned long vdl_align_down (unsigned long v, unsigned long align);
unsigned long vdl_align_up (unsigned long v, unsigned long align);


#endif /* VDL_UTILS_H */
