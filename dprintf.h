#ifndef DPRINTF_H
#define DPRINTF_H

/**
 * This function uses no global variable and is thus fairly safe
 * to call from any situation.
 */
void dprintf (const char *str, ...);

#define DPRINTF(str,...) \
  dprintf(str, __VA_ARGS__)

#endif /* DPRINTF_H */
