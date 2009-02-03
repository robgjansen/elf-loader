CFLAGS=-g3 -Wall -Werror

all: hello hello-mdl libtest.so 

hello.o: hello.c
hello: hello.o
hello-mdl: hello-mdl.o
	$(CC) $(LDFLAGS) -ldl -o $@ $<
libtest.o: libtest.c
	$(CC) $(CFLAGS) -fpic -c -o $@ $<
libtest.so: libtest.o
	$(CC) -shared -o $@ $^

clean:
	-rm -f *.o *~ 2>/dev/null
	-rm libtest.so hello hello-mdl 2>/dev/null