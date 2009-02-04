#DEBUG=-DDEBUG_ENABLE
#OPT=-O2
LDSO_SONAME=ldso
CFLAGS=-g3 -Wall -Werror $(DEBUG) $(OPT)
LDFLAGS=$(OPT)
VISIBILITY=-fvisibility=hidden

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)

all: ldso elfedit
	$(MAKE) -C test

LDSO_OBJECTS=\
stage1.o stage2.o avprintf-cb.o dprintf.o vdl-utils.o vdl.o system.o alloc.o glibc.o gdb.o i386/machine.o i386/stage0.o interp.o

# dependency rules.
i386/machine.o: config.h
glibc.o: config.h
ldso: $(LDSO_OBJECTS) ldso.version libvdl.so
# build rules.
%.o:%.c
	$(CC) $(CFLAGS) -DLDSO_SONAME=\"$(LDSO_SONAME)\" -fno-stack-protector  -I$(PWD) -I$(PWD)/i386 -fpic $(VISIBILITY) -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso:
# note: we should be using -nostartfiles below but doing so makes the linker
# stop to map the ELF header and program headers in the resulting PT_LOAD entry
# which is problematic.
	$(LD) $(LDFLAGS) --entry=stage0 -nostdlib -shared --version-script=ldso.version --soname=$(LDSO_SONAME) -o $@ $(LDSO_OBJECTS) $(LIBGCC)

# we have two generated files and need to build them.
ldso.version: readversiondef
	./readversiondef /lib/ld-linux.so.2 > $@
config.h:
	./extract-system-config.py --debug /usr/lib/debug/ld-linux.so.2 --config config.h
# build the program used to generate ldso.version
readversiondef.o: readversiondef.c
	$(CC) $(CFLAGS) -c -o $@ $^
readversiondef: readversiondef.o
	$(CC) $(LDFLAGS) -o $@ $^

libvdl.version: readversiondef
	./readversiondef /lib/libdl.so.2 > $@
libvdl.o: libvdl.c
	$(CC) $(CFLAGS) $(VISIBILITY) -fpic -o $@ -c $< 
libvdl.so: libvdl.o libvdl.version
	$(CC) $(LDFLAGS) -nostdlib -shared -Wl,--version-script=libvdl.version -o $@ $<

elfedit.o: elfedit.c
	$(CC) $(CFLAGS) -o $@ -c $<
elfedit: elfedit.o
	$(CC) $(LDFLAGS) -o $@ $^

clean: 
	-rm -f elfedit readversiondef core hello hello-ldso 2> /dev/null
	-rm -f ldso libmdl.so *.o  i386/*.o 2>/dev/null
	-rm -f *~ i386/*~ 2>/dev/null
	-rm -f \#* i386/\#* 2>/dev/null
	-rm -f config.h ldso.version 2>/dev/null
	$(MAKE) -C test clean