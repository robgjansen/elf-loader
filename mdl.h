#ifndef MDL_H
#define MDL_H

#include <stdint.h>
#include <sys/types.h>
#include "alloc.h"

struct MappedFile
{
  // The following fields are part of the ABI. Don't change them
  uint8_t *load_base;
  char *filename;
  uint8_t *dynamic;
  struct MappedFile *next;
  struct MappedFile *prev;

  // The following fields are not part of the ABI
  uint32_t count;
  uint32_t context;
  uint8_t *ro_map;
  uint32_t ro_map_size;
  uint8_t *rw_map;
  uint32_t rw_map_size;
};

struct Mdl
{
  // the following fields are part of the ABI. Don't touch them.
  int version; // always 1
  struct MappedFile *link_map;
  int (*breakpoint)(void);
  enum {
    MDL_CONSISTENT,
    MDL_ADD,
    MDL_DELETE
  } state;
  uint8_t *interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t next_context;
  struct Alloc alloc;
};

extern struct Mdl g_mdl;

void mdl_initialize (uint8_t *interpreter_load_base);
struct MappedFile *mdl_load_file (const char *filename);
void *mdl_malloc (size_t size);
void mdl_free (void *buffer, size_t size);
int mdl_strisequal (const char *a, const char *b);
int mdl_strlen (const char *str);
char *mdl_strdup (const char *str);
void mdl_memcpy (void *dst, const void *src, size_t len);

#endif /* MDL_H */
