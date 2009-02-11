#ifndef VDL_FILE_LIST_H
#define VDL_FILE_LIST_H

#include "vdl.h"

void vdl_utils_file_list_free (struct VdlFileList *list);
struct VdlFileList *vdl_utils_file_list_copy (struct VdlFileList *list);
struct VdlFileList *vdl_utils_file_list_append_one (struct VdlFileList *list, 
						    struct VdlFile *item);
struct VdlFileList *vdl_utils_file_list_append (struct VdlFileList *start, 
						struct VdlFileList *end);
struct VdlFileList *vdl_utils_file_list_reverse (struct VdlFileList *start);
struct VdlFileList *vdl_utils_file_list_prepend_one (struct VdlFileList *list, 
						     struct VdlFile *item);
void vdl_utils_file_list_unicize (struct VdlFileList *list);
struct VdlFileList *vdl_utils_file_list_remove (struct VdlFileList *list, 
						struct VdlFileList *item);
#endif /* VDL_FILE_LIST_H */
