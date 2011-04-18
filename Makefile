SRCDIR= $(dir $(firstword $(MAKEFILE_LIST)))
BLDDIR= $(PWD)/
#DEBUG=-DDPRINTF_DEBUG_ENABLE
DEBUG+=-DMALLOC_DEBUG_ENABLE
#OPT=-O2
LDSO_SONAME=ldso
VALGRIND_CFLAGS=$(shell $(SRCDIR)get-valgrind-cflags.py)
CFLAGS+=-g3 -Wall -Werror $(DEBUG) $(OPT) $(VALGRIND_CFLAGS) -D_GNU_SOURCE -Wp,-MD,.$*.d
CXXFLAGS+=$(CFLAGS)
LDFLAGS+=$(OPT)
INSTALL:=install

PWD=$(shell pwd)
ARCH=$(shell uname -m)/
ifeq ($(ARCH),i586/)
ARCH=i386/
else ifeq ($(ARCH),i686/)
ARCH=i386/
endif
ifeq ($(ARCH),i386/)
LDSO_FILE=/lib/ld-linux.so.2
LIBDL_FILE=/lib/libdl.so.2
else ifeq ($(ARCH),x86_64/)
LDSO_FILE=/lib64/ld-linux-x86-64.so.2
LIBDL_FILE=/lib64/libdl.so.2
endif

all: ldso libvdl.so elfedit internal-tests display-relocs

install: all
	$(INSTALL) -d $(PREFIX)/lib $(PREFIX)/bin
	$(INSTALL) -t $(PREFIX)/lib ldso libvdl.so 
	$(INSTALL) -t $(PREFIX)/bin  readversiondef elfedit 

test: FORCE internal-tests
	$(MAKE) -C test
	$(MAKE) -C test run
	./internal-tests

test-valgrind: FORCE
	$(MAKE) -C test
	$(MAKE) -C test run-valgrind

FORCE:

LDSO_ARCH_SRC=\

LDSO_SOURCE=\
avprintf-cb.c \
dprintf.c vdl-utils.c vdl-log.c \
vdl.c system.c alloc.c \
vdl-reloc.c \
vdl-gc.c vdl-lookup.c \
futex.c vdl-tls.c \
$(ARCH)stage0.S \
$(ARCH)machine.c $(ARCH)resolv.S \
vdl-sort.c vdl-mem.c \
vdl-list.c vdl-context.c \
vdl-alloc.c vdl-linkmap.c \
vdl-map.c vdl-unmap.c \
vdl-init.c \
vdl-fini.c \
interp.c gdb.c glibc.c \
stage1.c stage2.c  \
vdl-dl.c \
vdl-dl-public.c valgrind.c
SOURCE=$(LDSO_FULL_SOURCE) libvdl.c elfedit.c readversiondef.c

LDSO_OBJECTS=$(addprefix ,$(addsuffix .o,$(basename $(LDSO_SOURCE))))


ldso: $(LDSO_OBJECTS) ldso.version
# build rules.
C_CMD=$(CC) $(CFLAGS) -DLDSO_SONAME=\"$(LDSO_SONAME)\" -fno-stack-protector  -I$(SRCDIR) -I$(BLDDIR) -I$(SRCDIR)$(ARCH) -fpic -fvisibility=hidden -o $@ -c $<
%.o:$(SRCDIR)%.c
	$(C_CMD)
$(ARCH)%.o:$(SRCDIR)$(ARCH)%.c
	$(C_CMD)
$(ARCH)%.o:$(SRCDIR)$(ARCH)%.S
	@if test ! -d $(dir $@); then mkdir -p $(dir $@); fi
	$(AS) $(ASFLAGS) -o $@ $<
ldso: vdl-config.h
	$(CC) $(LDFLAGS) -shared -nostartfiles -nostdlib -Wl,--entry=stage0,--version-script=ldso.version,--soname=$(LDSO_SONAME) -o $@ $(LDSO_OBJECTS) -Wl,-Bstatic,-lgcc

# we have two generated files and need to build them.
ldso.version: readversiondef $(SRCDIR)vdl-dl.version
	./readversiondef $(LDSO_FILE) | cat $(SRCDIR)vdl-dl.version - > $@
vdl-config.h:
	$(SRCDIR)extract-system-config.py >$@
# build the program used to generate ldso.version
readversiondef.o: $(SRCDIR)readversiondef.c
	$(CC) $(CFLAGS) -c -o $@ $^
readversiondef: readversiondef.o
	$(CC) $(LDFLAGS) -o $@ $^

libdl.version: readversiondef $(SRCDIR)libvdl.version
	./readversiondef $(LIBDL_FILE) | cat $(SRCDIR)libvdl.version - > $@
libvdl.o: $(SRCDIR)libvdl.c
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
%.o:$(SRCDIR)%.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $^
internal-tests: $(TEST_OBJECT)
	$(CXX) $(LDFLAGS) $^ -o $@
display-relocs: display-relocs.o

clean: 
	-rm -f internal-tests elfedit readversiondef display-relocs core hello hello-ldso 2> /dev/null
	-rm -f ldso libvdl.so *.o  $(ARCH)/*.o 2>/dev/null
	-rm -f *~ $(ARCH)/*~ 2>/dev/null
	-rm -f \#* $(ARCH)/\#* 2>/dev/null
	-rm -f ldso.version libdl.version vdl-config.h 2>/dev/null
	-rm -f .*.d 2>/dev/null
	-rmdir $(ARCH) 2>/dev/null
	$(MAKE) -C test -f $(SRCDIR)test/Makefile clean

-include $(SRC:%.o=.deps/%.d)