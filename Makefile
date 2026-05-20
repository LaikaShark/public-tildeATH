CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
RUNTIME := runtime

.PHONY: all runtime test-runtime clean

all: runtime

runtime: $(RUNTIME)/libath_fresh.a $(RUNTIME)/libath_intern.a

$(RUNTIME)/libath_fresh.a: $(RUNTIME)/runtime_common.o $(RUNTIME)/compose_fresh.o
	ar rcs $@ $^

$(RUNTIME)/libath_intern.a: $(RUNTIME)/runtime_common.o $(RUNTIME)/compose_intern.o
	ar rcs $@ $^

$(RUNTIME)/runtime_common.o: $(RUNTIME)/runtime_common.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/compose_fresh.o: $(RUNTIME)/compose_fresh.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/compose_intern.o: $(RUNTIME)/compose_intern.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/test_runtime_fresh: $(RUNTIME)/test_runtime.c $(RUNTIME)/libath_fresh.a
	$(CC) $(CFLAGS) -I$(RUNTIME) $< $(RUNTIME)/libath_fresh.a -lm -o $@

$(RUNTIME)/test_runtime_intern: $(RUNTIME)/test_runtime.c $(RUNTIME)/libath_intern.a
	$(CC) $(CFLAGS) -DATH_INTERN_MODE -I$(RUNTIME) $< $(RUNTIME)/libath_intern.a -lm -o $@

test-runtime: $(RUNTIME)/test_runtime_fresh $(RUNTIME)/test_runtime_intern
	@echo "--- fresh ---"
	./$(RUNTIME)/test_runtime_fresh
	@echo "--- intern ---"
	./$(RUNTIME)/test_runtime_intern

clean:
	rm -f $(RUNTIME)/*.o $(RUNTIME)/*.a \
	      $(RUNTIME)/test_runtime $(RUNTIME)/test_runtime_fresh \
	      $(RUNTIME)/test_runtime_intern
