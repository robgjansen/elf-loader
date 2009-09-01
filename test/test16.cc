#include "test.h"
LIB(test16)

class Foo
{};

int main (int argc, char *argv[])
{
  throw Foo ();
  return 0;
}
