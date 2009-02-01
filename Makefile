#DEBUG=-DDEBUG_ENABLE
LDSO_SONAME=ldso
CFLAGS=-g3 -Wall $(DEBUG)
VISIBILITY=-fvisibility=hidden

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)

all: ldso elfedit hello hello-ldso

LDSO_OBJECTS=\
stage1.o stage2.o avprintf-cb.o dprintf.o mdl.o system.o alloc.o mdl-elf.o glibc.o gdb.o i386/machine.o i386/stage0.o

# dependency rules.
i386/machine.o: config.h
glibc.o: config.h
ldso: $(LDSO_OBJECTS) ldso.version
ldso: $(LDSO_OBJECTS)
# build rules.
%.o:%.c
	$(CC) $(CFLAGS) -DLDSO_SONAME=\"$(LDSO_SONAME)\" -fno-stack-protector  -I$(PWD) -I$(PWD)/i386 -fpic $(VISIBILITY) -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso:
	$(LD) $(LDFLAGS) -e stage0 -shared -nostdlib $(VISIBILITY) --version-script=ldso.version --dynamic-linker=ldso --soname=$(LDSO_SONAME) -o $@ $(LDSO_OBJECTS) $(LIBGCC)

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

hello.o: hello.c
	$(CC) $(CFLAGS) -o $@ -c $<
hello-ldso: hello.o
	$(CC) $(LDFLAGS) -Wl,--dynamic-linker=ldso -o $@ $^
hello: hello.o
	$(CC) $(LDFLAGS) -o $@ $^
elfedit.o: elfedit.c
	$(CC) $(CFLAGS) -o $@ -c $<
elfedit: elfedit.o
	$(CC) $(LDFLAGS) -o $@ $^

clean: 
	-rm -f elfedit readversiondef core hello hello-ldso 2> /dev/null
	-rm -f ldso *.o  i386/*.o 2>/dev/null
	-rm -f *~ i386/*~ 2>/dev/null
	-rm -f \#* i386/\#* 2>/dev/null
	-rm -f config.h ldso.version 2>/dev/null