#include "loader.h"

struct Loader _r_debug;
struct Loader *g_loader;

void loader_initialize (void)
{
  g_loader = &_r_debug;
}
