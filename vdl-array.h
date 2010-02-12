#ifndef VDL_ARRAY_H
#define VDL_ARRAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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
void vdl_array_low_remove (struct VdlArray *array, uint32_t at, uint32_t n);
uint32_t vdl_array_low_get_size (struct VdlArray *array);

#define vdl_array_new(type,n) \
  vdl_array_low_alloc (sizeof(type), n)
#define vdl_array_delete(array) \
  vdl_array_low_free (array)
#define vdl_array_get(array,i,type)		\
  *((type *) vdl_array_low_get (array, i))
#define vdl_array_set(array,i,value)				\
  *(typeof(value) *) vdl_array_low_get (array, i) = value
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
#define vdl_array_get_size(array)		\
  vdl_array_low_get_size (array)
#define vdl_array_begin(array,type)			\
  ((type *)vdl_array_low_get (array, 0))
#define vdl_array_end(array,type)			\
  ((type *)vdl_array_low_get (array,vdl_array_low_get_size (array)))
#ifdef __cplusplus
}
#endif

#endif /* VDL_ARRAY_H */
