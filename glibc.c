#include "glibc.h"

#define EXPORT __attribute__ ((visibility("default")))

int _dl_starting_up EXPORT = 0 ;

void glibc_startup_finished (void) 
{
  _dl_starting_up = 1;
}
