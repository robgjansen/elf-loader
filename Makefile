#DEBUG=-DDEBUG_ENABLE
LDSO_SONAME=ldso
CFLAGS=-g3 -fno-stack-protector -Wall $(DEBUG) -DLDSO_SONAME=\"$(LDSO_SONAME)\"

#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)

all: ldso elfedit hello hello-ldso

%.c: config.h

%.o:%.c
	$(CC) $(CFLAGS) -I$(PWD) -I$(PWD)/i386 -fpie -fvisibility=hidden -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso: ldso.o avprintf-cb.o dprintf.o mdl.o system.o alloc.o mdl-elf.o glibc.o gdb.o i386/machine.o i386/start-trampoline.o
	$(LD) $(LDFLAGS) -e stage1 -pie -nostdlib -fvisibility=hidden --dynamic-list=ldso.dyn --dynamic-linker=ldso --soname=$(LDSO_SONAME) -o $@ $^ $(LIBGCC)

config.h:
	readelf -wi /usr/lib/debug/ld-linux.so.2 | ./extract-system-config.py > $@

hello.o: hello.c
	$(CC) $(CFLAGS) -o $@ -c $<
hello-ldso: hello.o
	$(CC) $(LDFLAGS) -Wl,--dynamic-linker=ldso -o $@ $^
hello: hello.o
	$(CC) $(LDFLAGS) -o $@ $^
elfedit: elfedit.o

clean: 
	-rm -f elfedit core hello hello-ldso 2> /dev/null
	-rm -f ldso *.o  i386/*.o 2>/dev/null
	-rm -f *~ i386/*~ 2>/dev/null
	-rm -f \#* i386/\#* 2>/dev/null