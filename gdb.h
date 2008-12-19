#ifndef GDB_H
#define GDB_H

struct MappedFile;

void gdb_initialize (struct MappedFile *main);

void gdb_notify (void);

#endif /* GDB_H */
