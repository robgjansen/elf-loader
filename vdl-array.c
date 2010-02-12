#include "vdl-array.h"
#include "vdl-utils.h"
#include "vdl-mem.h"

struct VdlArray *vdl_array_low_alloc (uint32_t elem_size, uint32_t n)
{
  struct VdlArray *array = vdl_utils_new (struct VdlArray);
  array->n = n;
  array->max_n = n;
  array->elem_size = elem_size;
  array->buffer = vdl_utils_malloc (elem_size * n);
  return array;
}
void vdl_array_low_free (struct VdlArray *array)
{
  vdl_utils_free (array->buffer);
  array->n = 0;
  array->max_n = 0;
  array->elem_size = 0;
  array->buffer = 0;
  vdl_utils_delete (array);
}
uint8_t *vdl_array_low_get (struct VdlArray *array, uint32_t i)
{
  if (i > array->n)
    {
      return 0;
    }
  return &array->buffer[array->elem_size * i];
}
uint8_t *vdl_array_low_insert (struct VdlArray *array, uint32_t at, uint32_t n)
{
  if (at > array->n)
    {
      return 0;
    }
  if (array->n + n <= array->max_n)
    {
      vdl_memmove (array->buffer + (at + n) * array->elem_size,
		   array->buffer + (at) * array->elem_size,
		   n * array->elem_size);
      array->n += n;
    }
  else
    {
      uint8_t *new_buffer = vdl_utils_malloc (array->elem_size * (array->n + n));
      vdl_memcpy (new_buffer, array->buffer, at * array->elem_size);
      vdl_memset (new_buffer + at * array->elem_size, 0, n);
      vdl_memcpy (new_buffer + (at + n) * array->elem_size, 
		  array->buffer + at * array->elem_size,
			(array->n - at) * array->elem_size);
      vdl_utils_free (array->buffer);
      array->buffer = new_buffer;
      array->n += n;
      array->max_n = array->n;
    }
  return &array->buffer[at * array->elem_size];
}
uint8_t *vdl_array_low_remove (struct VdlArray *array, uint32_t at, uint32_t n)
{
  if (at > array->n)
    {
      return 0;
    }
  uint32_t remove_end = vdl_utils_min (at + n, array->n);
  vdl_memmove (array->buffer + at * array->elem_size,
	       array->buffer + remove_end * array->elem_size,
	       (array->n - remove_end) * array->elem_size);
  array->n -= remove_end - at;
  return (uint8_t *)(array->buffer + at * array->elem_size);
}
uint32_t vdl_array_low_size (struct VdlArray *array)
{
  return array->n;
}
uint8_t *vdl_array_low_find (struct VdlArray *array, uint8_t *value)
{
  uint8_t i;
  for (i = 0; i < array->n; i++)
    {
      uint8_t *p = &array->buffer[i*array->elem_size];
      if (vdl_memcmp (p, value, array->elem_size) == 0)
	{
	  return p;
	}
    }
  return 0;
}
