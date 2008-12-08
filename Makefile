CFLAGS=-g3 -O1 -fno-stack-protector
#we need libgcc for 64bit arithmetic functions
LIBGCC=$(shell gcc --print-libgcc-file-name)

all: ldso hello

%.o:%.c
	$(CC) $(CFLAGS) -fpie -o $@ -c $<
ldso.o: syscall.h
ldso: ldso.o avprintf-cb.o dprintf.o
	$(LD) $(LDFLAGS) -e _dl_start -pie -nostdlib --dynamic-linker=ldso -o $@ $^ $(LIBGCC)

hello: hello.o
	$(CC) $(LDFLAGS) -Wl,--dynamic-linker=ldso -o $@ $^

clean: 
	-rm -f hello ldso *.o  2> /dev/null