#ifndef GLIBC_H
#define GLIBC_H

struct VdlFileList;

// Interfaces needed to make glibc be able to work when
// loaded with this loader.

// set _dl_starting_up to 1. Must be called
// just before calling the executable's entry point.
void glibc_startup_finished (void);

void glibc_initialize (void);

void glibc_patch (struct VdlFileList *files);


#endif /* GLIBC_H */
