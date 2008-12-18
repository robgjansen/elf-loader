#include "glibc.h"

int _dl_starting_up = 0;

void glibc_startup_finished (void)
{
  _dl_starting_up = 1;
}
