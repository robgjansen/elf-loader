#ifndef VDL_TLS_H
#define VDL_TLS_H

#include <stdbool.h>

// called prior initial relocation processing. 
// collect and store tls information about everything
// in g_vdl and each file
void vdl_tls_initialize (void);
// allocate a tcb buffer
unsigned long vdl_tls_tcb_allocate (void);
// setup the sysinfo field in tcb
void vdl_tls_tcb_initialize (unsigned long tcb, unsigned long sysinfo);
// allocate a dtv vector and set it in the tcb
void vdl_tls_dtv_allocate (unsigned long tcb);
// The job of this function is to:
//    - initialize each static entry in the dtv to point to the right tls module block
//    - initialize each dynamic entry in the dtv to the UNALLOCATED value (0)
//    - initialize the content of each static tls module block with the associated
//      template
//    - initialize the dtv generation counter
void vdl_tls_dtv_initialize (unsigned long tcb);
// initialize per-file tls information
void vdl_tls_file_initialize (struct VdlFile *file);
void vdl_tls_dtv_deallocate (unsigned long tcb);
void vdl_tls_tcb_deallocate (unsigned long tcb);
// no need to call the _fast version with any kind of lock held
unsigned long vdl_tls_get_addr_fast (unsigned long module, unsigned long offset);
// the _slow version needs a lock held
unsigned long vdl_tls_get_addr_slow (unsigned long module, unsigned long offset);

bool vdl_tls_file_list_has_static (struct VdlFileList *list);
void vdl_tls_file_deinitialize (struct VdlFile *file);

#endif /* VDL_TLS_H */
