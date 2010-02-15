#DEBUG=-DPRINTF_DEBUG_ENABLE
DEBUG+=-DMALLOC_DEBUG_ENABLE
#OPT=-O2
LDSO_SONAME=ldso
VALGRIND_CFLAGS=$(shell ./get-valgrind-cflags.py)
CFLAGS=-g3 -Wall -Werror $(DEBUG) $(OPT) $(VALGRIND_CFLAGS) -D_GNU_SOURCE -Wp,-M,-MF,.deps/$*.d
CXXFLAGS=$(CFLAGS)
LDFLAGS=$(OPT)

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)
ARCH=$(shell uname -m)
ifeq ($(ARCH),i586)
ARCH=i386
else ifeq ($(ARCH),i686)
ARCH=i386
endif
ifeq ($(ARCH),i386)
LDSO_FILE=/lib/ld-linux.so.2
LIBDL_FILE=/lib/libdl.so.2
LDSO_DEBUG_FILE=/usr/lib/debug/ld-linux.so.2
else ifeq ($(ARCH),x86_64)
LDSO_FILE=/lib64/ld-linux-x86-64.so.2
LIBDL_FILE=/lib64/libdl.so.2
LDSO_DEBUG_FILE=/usr/lib/debug/lib64/ld-linux-x86-64.so.2.debug
endif

all: ldso libvdl.so elfedit internal-tests display-relocs

test: FORCE internal-tests
	$(MAKE) -C test
	$(MAKE) -C test run
	./internal-tests

test-valgrind: FORCE
	$(MAKE) -C test
	$(MAKE) -C test run-valgrind

FORCE:

LDSO_ARCH_SRC=\

LDSO_COMMON_SOURCE=\
avprintf-cb.c \
dprintf.c vdl-utils.c vdl-log.c \
vdl.c system.c alloc.c \
vdl-reloc.c \
vdl-gc.c vdl-lookup.c \
futex.c vdl-tls.c \
$(ARCH)/machine.c $(ARCH)/resolv.S \
vdl-init-fini.c vdl-sort.c  \
vdl-mem.c \
vdl-list.c
LDSO_SOURCE=$(LDSO_COMMON_SOURCE) \
interp.c gdb.c glibc.c \
stage1.c stage2.c  \
$(ARCH)/stage0.S vdl-dl.c \
vdl-dl-public.c valgrind.c
SOURCE=$(LDSO_FULL_SOURCE) libvdl.c elfedit.c readversiondef.c

LDSO_OBJECTS=$(addsuffix .o,$(basename $(LDSO_SOURCE)))
LDSO_COMMON_OBJECTS=$(addsuffix .o,$(basename $(LDSO_COMMON_SOURCE)))



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
vdl-config.h:
	./extract-system-config.py --debug=$(LDSO_DEBUG_FILE) >$@
# build the program used to generate ldso.version
readversiondef.o: readversiondef.c
	$(CC) $(CFLAGS) -c -o $@ $^
readversiondef: readversiondef.o
	$(CC) $(LDFLAGS) -o $@ $^

libdl.version: readversiondef libvdl.version
	./readversiondef $(LIBDL_FILE) | cat libvdl.version - > $@
libvdl.o: libvdl.c
	$(CC) $(CFLAGS) -fvisibility=hidden -fpic -o $@ -c $< 
libvdl.so: libvdl.o ldso libdl.version
	$(CC) $(LDFLAGS) ldso -nostdlib -shared -Wl,--version-script=libdl.version -o $@ $<

elfedit: elfedit.o
internal-tests: LDFLAGS+=-lpthread
TEST_SOURCE = \
internal-tests.cc \
internal-test-alloc.cc \
internal-test-futex.cc \
internal-test-list.cc \
alloc.c \
futex.c \
vdl-list.c
TEST_OBJECT = $(addsuffix .o,$(basename $(TEST_SOURCE)))
internal-tests: $(TEST_OBJECT)
	$(CXX) $(LDFLAGS) $^ -o $@
display-relocs: display-relocs.o

clean: 
	-rm -f internal-tests elfedit readversiondef display-relocs core hello hello-ldso 2> /dev/null
	-rm -f ldso libvdl.so *.o  $(ARCH)/*.o 2>/dev/null
	-rm -f *~ $(ARCH)/*~ 2>/dev/null
	-rm -f \#* $(ARCH)/\#* 2>/dev/null
	-rm -f ldso.version 2>/dev/null
	$(MAKE) -C test clean

-include $(SRC:%.o=.deps/%.d)