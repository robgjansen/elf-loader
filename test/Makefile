CFLAGS=-g3 -Wall -Werror
LDFLAGS=-L.

TESTS=test0 test01 test1 test2 test3 test4 test5 test6 test7 test8 test8.5 test9 test10
TARGETS=hello libi.so libh.so libg.so libf.so libe.so libd.so libb.so liba.so \
 $(TESTS) $(addsuffix -ldso,$(TESTS))

all: $(TARGETS)

OUTPUT_DIR=output
generate-ref: $(addsuffix .ref,$(addprefix $(OUTPUT_DIR)/,$(TESTS)))
$(OUTPUT_DIR)/test%.ref: test%
	LD_LIBRARY_PATH=. ./$^ > $@
run: $(TARGETS) $(addprefix run-,$(TESTS))
test%-ldso: test%
	@cp $^ $@
	@../elfedit $@ ../ldso
run-test%: test%-ldso FORCE
	@-LD_LIBRARY_PATH=.:../ ./$< > $(OUTPUT_DIR)/test$* 2>/dev/null || true;
	@-diff -q $(OUTPUT_DIR)/test$* $(OUTPUT_DIR)/test$*.ref > /dev/null; \
		if test $$? -eq 0; then echo "PASS $@"; else echo "FAIL $@  -- LD_LIBRARY_PATH=.:../ ./$<"; fi
FORCE:
.SECONDARY:



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
libg.so: LDFLAGS+=-lh
libh.so:
test0: LDFLAGS+=-la
test01: LDFLAGS+=-lb -la
test8.5: LDFLAGS+=-lpthread
test9: LDFLAGS+=-lpthread
test10: LDFLAGS+=-lpthread

clean:
	-rm -f *.o *~ 2>/dev/null
	-rm -f $(TARGETS) 2>/dev/null
	-rm -f $(addprefix $(OUTPUT_DIR)/,$(TESTS)) 2>/dev/null
	-rm -f $(addsuffix -ldso,$(TESTS)) 2>/dev/null