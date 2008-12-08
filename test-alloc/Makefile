CFLAGS=-g3 -O0
CXXFLAGS=-g3 -O0
test-alloc: test-alloc.o alloc.o system.o
	$(CXX) $(LDFLAGS) $^ -o $@
alloc.o: ../alloc.c
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	-rm -f *.o test-alloc 2>/dev/null
