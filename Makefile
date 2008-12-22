#DEBUG=-DDEBUG_ENABLE
CFLAGS=-g3 -fno-stack-protector -Wall $(DEBUG)
#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)
PWD=$(shell pwd)

all: ldso hello

%.o:%.c
	$(CC) $(CFLAGS) -I$(PWD) -I$(PWD)/i386 -fpie -fvisibility=hidden -o $@ -c $<
%.o:%.S
	$(AS) $(ASFLAGS) -o $@ $<
ldso: ldso.o avprintf-cb.o dprintf.o mdl.o system.o alloc.o mdl-elf.o glibc.o gdb.o i386/machine.o i386/start-trampoline.o
	$(LD) $(LDFLAGS) -e stage1 -pie -nostdlib -fvisibility=hidden --dynamic-list=ldso.dyn --dynamic-linker=ldso -o $@ $^ $(LIBGCC)

hello.o: hello.c
	$(CC) $(CFLAGS) -o $@ -c $<
hello: hello.o
	$(CC) $(LDFLAGS) -Wl,--dynamic-linker=ldso -o $@ $^

clean: 
	-rm -f hello ldso *.o  i386/*.o 2> /dev/null