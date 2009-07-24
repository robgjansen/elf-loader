#include <iostream>

bool test_alloc (void);
bool test_futex (void);

#define RUN_TEST(name)					\
  do {							\
    bool result = test_##name ();			\
    if (!result) {ok = false;}				\
    const char *result_string = result?"PASS":"FAIL";	\
    std::cout << #name << "=" << result_string << std::endl;	\
  } while (false)

int main (int argc, char *argv[])
{
  bool ok = true;
  RUN_TEST (alloc);
  RUN_TEST (futex);
  return ok?0:1;
}
