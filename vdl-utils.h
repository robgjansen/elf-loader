#ifndef VDL_UTILS_H
#define VDL_UTILS_H

#include <sys/types.h>
#include <stdarg.h>
#include "vdl.h"
#include "vdl-list.h"

void vdl_utils_linkmap_print (void);

struct VdlList * vdl_utils_list_global_linkmap_new (void);


// string manipulation functions
int vdl_utils_strisequal (const char *a, const char *b);
int vdl_utils_strlen (const char *str);
char *vdl_utils_strdup (const char *str);
char *vdl_utils_strfind (char *str, const char *substr);
char *vdl_utils_strconcat (const char *str, ...);
const char *vdl_utils_getenv (const char **envp, const char *value);

// convenience function
int vdl_utils_exists (const char *filename);

// manipulate lists of strings.
void vdl_utils_str_list_delete (struct VdlList *list);
struct VdlList *vdl_utils_strsplit (const char *value, char separator);
struct VdlList *vdl_utils_splitpath (const char *value);

unsigned long vdl_utils_align_down (unsigned long v, unsigned long align);
unsigned long vdl_utils_align_up (unsigned long v, unsigned long align);

#define vdl_utils_max(a,b)(((a)>(b))?(a):(b))
#define vdl_utils_min(a,b)(((a)<(b))?(a):(b))

ElfW(Phdr) *vdl_utils_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);

char *vdl_utils_vprintf (const char *str, va_list args);

#endif /* VDL_UTILS_H */
