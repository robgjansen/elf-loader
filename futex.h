#ifndef FUTEX_H
#define FUTEX_H

#include <stdint.h>

struct futex
{
  uint32_t state __attribute__ ((aligned(4)));
};

void futex_init (struct futex *futex);
void futex_lock (struct futex *futex);
void futex_unlock (struct futex *futex);

#endif /* FUTEX_H */
