CFLAGS=-g3 -Wall -Werror
LDFLAGS=-L.

TESTS=test0 test0_1 test0_2 test1 test2 test3 test4 test5 test6 test7 test8 test8_5 test9 test10 test11 test12
TARGETS=hello libl.so libk.so libj.so libi.so libh.so libg.so libf.so libe.so libd.so libb.so liba.so \
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
	@-$(TEST_PRE_COMMAND)
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
libg.so: LDFLAGS+=-lh
libj.so: LDFLAGS+=-lk
libk.so: LDFLAGS+=-ll
test0: LDFLAGS+=-la
test0_1: LDFLAGS+=-lb -la
test8_5: LDFLAGS+=-lpthread
test9: LDFLAGS+=-lpthread
test10: LDFLAGS+=-lpthread
test11:
test12: LDFLAGS+=-ldl
run-test12: TEST_PRE_COMMAND=rm libl.so

clean:
	-rm -f *.o *~ 2>/dev/null
	-rm -f $(TARGETS) 2>/dev/null
	-rm -f $(addprefix $(OUTPUT_DIR)/,$(TESTS)) 2>/dev/null
	-rm -f $(addsuffix -ldso,$(TESTS)) 2>/dev/null