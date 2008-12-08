#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>
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

struct Loader
{
  // the following fields are part of the ABI. Don't touch them.
  int version; // always 1
  struct MappedFile *link_map;
  int (*breakpoint)(void);
  enum {
    LOADER_CONSISTENT,
    LOADER_ADD,
    LOADER_DELETE
  } state;
  uint8_t *interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t next_context;
  struct Alloc alloc;
  char *(*lookup_filename) (const char *filename);
  char *(*lookup_symbol) (const char *symbol);
};

extern struct Loader *g_loader;

void loader_initialize (void);

#endif /* LOADER_H */
