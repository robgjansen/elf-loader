CFLAGS=-g3 -O1 -fno-stack-protector

all: ldso hello

stage-1.o: stage-1.c syscall.h
	$(CC) $(CFLAGS) -fpie -o $@ -c $<
ldso: stage-1.o
	$(LD) $(LDFLAGS) -e _dl_start -pie -nostdlib --dynamic-linker=ldso -o $@ $^

hello: hello.o
	$(CC) $(LDFLAGS) -Wl,--dynamic-linker=ldso -o $@ $^

clean: 
	-rm -f hello ldso *.o  2> /dev/null