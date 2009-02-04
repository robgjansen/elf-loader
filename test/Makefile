CFLAGS=-g3 -Wall -Werror
LDFLAGS=-L.

TARGETS=hello hello-dlopen libe.so libd.so libb.so liba.so libtest.so

all: $(TARGETS)

hello.o: hello.c
hello: hello.o
hello-dlopen: hello-dlopen.o
	$(CC) $(LDFLAGS) -ldl -o $@ $<
lib%.o: lib%.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<
lib%.so:lib%.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

libtest.so: LDFLAGS+=-la -lb
liba.so: LDFLAGS+=-ld
libb.so: LDFLAGS+=-ld
libd.so: LDFLAGS+=-le

clean:
	-rm -f *.o *~ 2>/dev/null
	-rm $(TARGETS) 2>/dev/null