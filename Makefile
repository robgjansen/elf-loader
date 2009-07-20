#DEBUG=-DDEBUG_ENABLE
#OPT=-O2
LDSO_SONAME=ldso
CFLAGS=-g3 -Wall -Werror $(DEBUG) $(OPT)
LDFLAGS=$(OPT)

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)
ARCH=$(shell uname -p)
ifeq ($(ARCH),i386)
LDSO_FILE=/lib/ld-linux.so.2
LIBDL_FILE=/lib/libdl.so.2
LDSO_DEBUG_FILE=/usr/lib/debug/ld-linux.so.2
else ifeq ($(ARCH),x86_64)
LDSO_FILE=/lib64/ld-linux-x86-64.so.2
LIBDL_FILE=/lib64/libdl.so.2
LDSO_DEBUG_FILE=/usr/lib/debug/lib64/ld-linux-x86-64.so.2.debug
endif


all: ldso libvdl.so elfedit

test: FORCE
	$(MAKE) -C test
	$(MAKE) -C test run
FORCE:

LDSO_ARCH_OBJECTS=\
$(ARCH)/machine.o $(ARCH)/stage0.o $(ARCH)/resolv.o 
LDSO_OBJECTS=\
stage1.o stage2.o avprintf-cb.o \
dprintf.o vdl-utils.o vdl-log.o \
vdl.o system.o alloc.o glibc.o \
gdb.o vdl-dl.o interp.o vdl-file-reloc.o \
vdl-file-list.o vdl-gc.o vdl-file-symbol.o \
futex.o $(LDSO_ARCH_OBJECTS)

# dependency rules.
$(ARCH)/machine.o: config.h
glibc.o: config.h
ldso: $(LDSO_OBJECTS) ldso.version
# build rules.
%.o:%.c
	$(CC) $(CFLAGS) -DLDSO_SONAME=\"$(LDSO_SONAME)\" -fno-stack-protector  -I$(PWD) -I$(PWD)/$(ARCH) -fpic -fvisibility=hidden -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso:
	$(CC) $(LDFLAGS) -shared -nostartfiles -nostdlib -Wl,--entry=stage0,--version-script=ldso.version,--soname=$(LDSO_SONAME) -o $@ $(LDSO_OBJECTS) $(LIBGCC)

# we have two generated files and need to build them.
ldso.version: readversiondef vdl-dl.version
	./readversiondef $(LDSO_FILE) | cat vdl-dl.version - > $@
config.h:
	./extract-system-config.py --debug=$(LDSO_DEBUG_FILE) >$@
# build the program used to generate ldso.version
readversiondef.o: readversiondef.c
	$(CC) $(CFLAGS) -c -o $@ $^
readversiondef: readversiondef.o
	$(CC) $(LDFLAGS) -o $@ $^

libvdl.version: readversiondef
	./readversiondef $(LIBDL_FILE) > $@
libvdl.o: libvdl.c
	$(CC) $(CFLAGS) -fvisibility=hidden -fpic -o $@ -c $< 
libvdl.so: libvdl.o ldso libvdl.version
	$(CC) $(LDFLAGS) ldso -nostdlib -shared -Wl,--version-script=libvdl.version -o $@ $<

elfedit.o: elfedit.c
	$(CC) $(CFLAGS) -o $@ -c $<
elfedit: elfedit.o
	$(CC) $(LDFLAGS) -o $@ $^

clean: 
	-rm -f elfedit readversiondef core hello hello-ldso 2> /dev/null
	-rm -f ldso libvdl.so *.o  $(ARCH)/*.o 2>/dev/null
	-rm -f *~ $(ARCH)/*~ 2>/dev/null
	-rm -f \#* $(ARCH)/\#* 2>/dev/null
	-rm -f ldso.version 2>/dev/null
	$(MAKE) -C test clean