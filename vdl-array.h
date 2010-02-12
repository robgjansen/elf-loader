#ifndef VDL_ARRAY_H
#define VDL_ARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


/**
 * The API exported by this file is heavily inspired from the
 * C++ std::vector template class. The naming as well as functionality
 * are exactly equivalent. The current implementation is slow as hell
 * but I don't care for now. It should be trivial to optimize the
 * number of memory allocations to make it amortized O(1) but, again,
 * that's not an issue for now.
 */

#define vdl_array_new(type) \
  vdl_array_low_alloc (sizeof(type), 0)
#define vdl_array_new_with_size(type,size)		\
  vdl_array_low_alloc (sizeof(type), size)
#define vdl_array_delete(array) \
  vdl_array_low_free (array)
#define vdl_array_at(array,i,type)		\
  (*((type *) vdl_array_low_get (array, i)))
#define vdl_array_set(array,i,value)				\
  vdl_array_at (array,i, typeof(value)) = value
#define vdl_array_insert(array,at,value)			\
  {								\
    typeof(value) *_value = (typeof(value) *)			\
      vdl_array_low_insert (array, at, 1);			\
    *_value = value;						\
  }
#define vdl_array_push_back(array,value)				\
  {									\
    typeof(value) *_value = (typeof(value) *)				\
      vdl_array_low_insert (array,					\
			    vdl_array_low_get_size (array),		\
			    1);						\
    *_value = value;							\
  }
#define vdl_array_push_front(array,value)				\
  {									\
    typeof(value) *_value = (typeof(value) *)				\
      vdl_array_low_insert (array,					\
			    0,						\
			    1);						\
    *_value = value;							\
  }
#define vdl_array_pop_front(array) \
  vdl_array_remove(array,0)
#define vdl_array_pop_back(array) \
  vdl_array_remove(array,vdl_array_get_size(array)-1)
#define vdl_array_front(array,type)		\
  vdl_array_get (array,0,type)
#define vdl_array_back(array,type)				\
  vdl_array_get (array,vdl_array_get_size (array)-1,type)
#define vdl_array_remove(array, at)		\
  vdl_array_low_remove (array, at, 1)
#define vdl_array_size(array)		\
  vdl_array_low_get_size (array)
#define vdl_array_empty(array)		\
  (vdl_array_low_get_size (array) == 0)
#define vdl_array_erase(array,i)					\
  ((type *)vdl_array_low_remove (array,					\
				 (((unsigned long)i) - ((unsigned long)array->buffer)) \
				 / array->elem_size,			\
				 1)
#define vdl_array_peek(array,i,type)		\
  *((type *) vdl_array_low_get (array, i))  
#define vdl_array_clear(array)			\
  vdl_array_low_remove (array, 0, array->n)

// warning: these 2 macros will not work in C++
#define vdl_array_begin(array)			\
  ((void *)vdl_array_low_get (array, 0))
#define vdl_array_end(array)			\
  ((void *)vdl_array_low_get (array,vdl_array_low_get_size (array)))


/* don't use the _low functions below. Use the macros above. */

struct VdlArray
{
  uint32_t n;
  uint32_t max_n;
  uint32_t elem_size;
  uint8_t *buffer;
};
struct VdlArray *vdl_array_low_alloc (uint32_t elem_size, uint32_t n);
void vdl_array_low_free (struct VdlArray *array);
uint8_t *vdl_array_low_get (struct VdlArray *array, uint32_t i);
uint8_t *vdl_array_low_insert (struct VdlArray *array, uint32_t at, uint32_t n);
uint8_t *vdl_array_low_remove (struct VdlArray *array, uint32_t at, uint32_t n);
uint32_t vdl_array_low_get_size (struct VdlArray *array);

#ifdef __cplusplus
}
#endif

#endif /* VDL_ARRAY_H */
