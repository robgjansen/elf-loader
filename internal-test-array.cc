#include <list>
#include <string.h>
#include "vdl-array.h"
#include "internal-test.h"

bool test_array (void)
{
  struct VdlArray *a = vdl_array_new (int, 0);
  vdl_array_delete (a);
  a = vdl_array_new (int, 0);
  INTERNAL_TEST_ASSERT_EQ (vdl_array_get_size (a), 0);
  vdl_array_append (a, 10);
  vdl_array_prepend (a, 5);
  INTERNAL_TEST_ASSERT_EQ (vdl_array_get_size (a), 2);
  INTERNAL_TEST_ASSERT_EQ (vdl_array_get (a, 0, int), 5);
  INTERNAL_TEST_ASSERT_EQ (vdl_array_get (a, 1, int), 10);
  vdl_array_delete (a);
  return true;
}
