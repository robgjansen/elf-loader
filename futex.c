#include "futex.h"
#include "machine.h"
#include "system.h"

void futex_init (struct futex *futex)
{
  futex->state = 0;
}

void futex_lock (struct futex *futex)
{
  uint32_t c;
  if ((c = machine_atomic_compare_and_exchange (&futex->state, 0, 1)) != 0)
    {
      do {
	if (c == 2 || machine_atomic_compare_and_exchange (&futex->state, 1, 2) != 0)
	  {
	    system_futex_wait (&futex->state, 2);
	  }
      } while ((c = machine_atomic_compare_and_exchange (&futex->state, 0, 2)) != 0);
    }
}

void futex_unlock (struct futex *futex)
{
  if (machine_atomic_dec (&futex->state) != 1) 
    {
      futex->state = 0;
      system_futex_wake (&futex->state, 1);
    }
}
