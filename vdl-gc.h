#ifndef VDL_GC_H
#define VDL_GC_H

/* Perform a mark and sweep garbage tri-colour collection 
 * of all VdlFile objects and returns the list of objects 
 * which can be freed.
 */
void vdl_gc (void);

#endif /* VDL_GC_H */
