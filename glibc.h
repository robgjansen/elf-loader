#ifndef GLIBC_H
#define GLIBC_H

struct MappedFile;

// Interfaces needed to make glibc be able to work when
// loaded with this loader.

// set _dl_starting_up to 1. Must be called
// just before calling the executable's entry point.
void glibc_startup_finished (void);

void glibc_initialize (unsigned long sysinfo);

void glibc_patch (struct MappedFile *file);


#endif /* GLIBC_H */
