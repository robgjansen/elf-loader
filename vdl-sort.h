#ifndef VDL_SORT_H
#define VDL_SORT_H

struct VdlFileList;
struct VdlFile;

// must free return value with vdl_file_list_free
struct VdlFileList *vdl_sort_increasing_depth (struct VdlFileList *files);
struct VdlFileList *vdl_sort_deps_breadth_first (struct VdlFile *file);
struct VdlFileList *vdl_sort_call_init (struct VdlFileList *files);
struct VdlFileList *vdl_sort_call_fini (struct VdlFileList *files);

#endif /* VDL_SORT_H */
