CC      ?= gcc
# -fPIC so the same objects feed both the static archives (linked into
# executables) and the shared libraries the REPL loads via ctypes.
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -fPIC
RUNTIME := runtime

.PHONY: all runtime test-runtime clean

all: runtime

runtime: $(RUNTIME)/libath_fresh.a $(RUNTIME)/libath_intern.a \
         $(RUNTIME)/libath_fresh.so $(RUNTIME)/libath_intern.so
	@# Keep the editable-install bundle (athc/_runtime/, what the CLI links against by
	@# default) in sync with the freshly built artifacts; setup.py does the same at build time.
	@mkdir -p athc/_runtime
	@cp -p $^ athc/_runtime/

$(RUNTIME)/libath_fresh.a: $(RUNTIME)/runtime_common.o $(RUNTIME)/scheduler.o $(RUNTIME)/net.o $(RUNTIME)/compose_fresh.o $(RUNTIME)/bigint.o
	ar rcs $@ $^

$(RUNTIME)/libath_intern.a: $(RUNTIME)/runtime_common.o $(RUNTIME)/scheduler.o $(RUNTIME)/net.o $(RUNTIME)/compose_intern.o $(RUNTIME)/bigint.o
	ar rcs $@ $^

# Shared libraries for the REPL (loaded via ctypes, athc/runtime_ffi.py).
$(RUNTIME)/libath_fresh.so: $(RUNTIME)/runtime_common.o $(RUNTIME)/scheduler.o $(RUNTIME)/net.o $(RUNTIME)/compose_fresh.o $(RUNTIME)/bigint.o
	$(CC) -shared $^ -lm -o $@

$(RUNTIME)/libath_intern.so: $(RUNTIME)/runtime_common.o $(RUNTIME)/scheduler.o $(RUNTIME)/net.o $(RUNTIME)/compose_intern.o $(RUNTIME)/bigint.o
	$(CC) -shared $^ -lm -o $@

$(RUNTIME)/runtime_common.o: $(RUNTIME)/runtime_common.c $(RUNTIME)/ath_runtime.h $(RUNTIME)/bigint.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/scheduler.o: $(RUNTIME)/scheduler.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/net.o: $(RUNTIME)/net.c $(RUNTIME)/ath_runtime.h
	$(CC) $(CFLAGS) -I$(RUNTIME) -c $< -o $@

$(RUNTIME)/bigint.o: $(RUNTIME)/bigint.c $(RUNTIME)/bigint.h
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
	rm -f $(RUNTIME)/*.o $(RUNTIME)/*.a $(RUNTIME)/*.so \
	      $(RUNTIME)/test_runtime $(RUNTIME)/test_runtime_fresh \
	      $(RUNTIME)/test_runtime_intern
	find . -name a.out -delete
