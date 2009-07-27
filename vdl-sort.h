#ifndef VDL_SORT_H
#define VDL_SORT_H

struct VdlFileList;
struct VdlFile;

struct VdlFileList *vdl_sort_deps_breadth_first (struct VdlFileList *files);
struct VdlFileList *vdl_sort_deps_breadth_first_one (struct VdlFile *file);
struct VdlFileList *vdl_sort_call_init (struct VdlFileList *files);
struct VdlFileList *vdl_sort_call_fini (struct VdlFileList *files);

#endif /* VDL_SORT_H */
