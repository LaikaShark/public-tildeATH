CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2
RUNTIME := runtime

.PHONY: all runtime test-runtime clean

all: runtime

runtime: $(RUNTIME)/libath_fresh.a

$(RUNTIME)/libath_fresh.a: $(RUNTIME)/runtime_fresh.o
	ar rcs $@ $<

$(RUNTIME)/runtime_fresh.o: $(RUNTIME)/runtime_fresh.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/test_runtime: $(RUNTIME)/test_runtime.c $(RUNTIME)/libath_fresh.a
	$(CC) $(CFLAGS) -I$(RUNTIME) $< $(RUNTIME)/libath_fresh.a -o $@

test-runtime: $(RUNTIME)/test_runtime
	./$(RUNTIME)/test_runtime

clean:
	rm -f $(RUNTIME)/*.o $(RUNTIME)/*.a $(RUNTIME)/test_runtime
