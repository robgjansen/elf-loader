CFLAGS=-g3 -Wall -Werror
LDFLAGS=-L.

TARGETS=hello test1 test2 test3 test4 libf.so libe.so libd.so libb.so liba.so

all: $(TARGETS)

hello.o: hello.c
hello: hello.o

lib%.o: lib%.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<
lib%.so:lib%.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

test%.o: test%.c
	$(CC) $(CFLAGS) -c -o $@ $<
test%: test%.o
	$(CC) $(LDFLAGS) -ldl -o $@ $<

liba.so: LDFLAGS+=-lb
libb.so: LDFLAGS+=-ld -le
libd.so: LDFLAGS+=-lf
libe.so: LDFLAGS+=-lf
libf.so:

clean:
	-rm -f *.o *~ 2>/dev/null
	-rm $(TARGETS) 2>/dev/null