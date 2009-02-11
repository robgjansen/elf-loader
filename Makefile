#DEBUG=-DDEBUG_ENABLE
#OPT=-O2
LDSO_SONAME=ldso
CFLAGS=-g3 -Wall -Werror $(DEBUG) $(OPT)
LDFLAGS=$(OPT)

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)

all: ldso libvdl.so elfedit
	$(MAKE) -C test

test: FORCE
	$(MAKE) -C test run
FORCE:

LDSO_OBJECTS=\
stage1.o stage2.o avprintf-cb.o dprintf.o vdl-utils.o vdl-log.o vdl.o system.o alloc.o glibc.o gdb.o vdl-dl.o i386/machine.o i386/stage0.o interp.o vdl-file-iter-rel.o
# vdl-gc.o

# dependency rules.
i386/machine.o: config.h
glibc.o: config.h
ldso: $(LDSO_OBJECTS) ldso.version
# build rules.
%.o:%.c
	$(CC) $(CFLAGS) -DLDSO_SONAME=\"$(LDSO_SONAME)\" -fno-stack-protector  -I$(PWD) -I$(PWD)/i386 -fpic -fvisibility=hidden -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso:
	$(CC) $(LDFLAGS) -shared -nostartfiles -nostdlib -Wl,--entry=stage0,--version-script=ldso.version,--soname=$(LDSO_SONAME) -o $@ $(LDSO_OBJECTS) $(LIBGCC)

# we have two generated files and need to build them.
ldso.version: readversiondef vdl-dl.version
	./readversiondef /lib/ld-linux.so.2 | cat vdl-dl.version - > $@
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
	$(CC) $(CFLAGS) -fvisibility=hidden -fpic -o $@ -c $< 
libvdl.so: libvdl.o ldso libvdl.version
	$(CC) $(LDFLAGS) ldso -nostdlib -shared -Wl,--version-script=libvdl.version -o $@ $<

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