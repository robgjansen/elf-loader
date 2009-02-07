#include "test.h"

LIB(h)

extern void function_f (void);

void function_h_f (void)
{
  function_f ();
}
