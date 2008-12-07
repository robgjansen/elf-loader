#ifndef DPRINTF_H
#define DPRINTF_H

void dprintf (const char *str, ...);

#define DPRINTF(str,...) \
  dprintf(str, __VA_ARGS__)

#endif /* DPRINTF_H */
