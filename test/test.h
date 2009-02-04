#ifndef TEST_H
#define TEST_H
#include <stdio.h>
#define LIB(name)				\
  static __attribute__ ((constructor))		\
  void constructor (void)			\
  {						\
    printf ("lib%s constructor\n", name);	\
  }						\
  static __attribute__ ((destructor))		\
  void destructor (void)			\
  {						\
    printf ("lib%s destructor\n", name);	\
  }
#endif
