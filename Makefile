CC = g++
CFLAGS_LIB = -Wall -Wextra -pedantic -fPIC -shared
CFLAGS_BIN = -Wall -Wextra -pedantic -pthread -std=c++17 -DWORKERS_COUNT=4

all: libcaesar.so caesar_cli

libcaesar.so: libcaesar.cpp
	$(CC) $(CFLAGS_LIB) -o $@ $<

caesar_cli: main.cpp libcaesar.so
	$(CC) $(CFLAGS_BIN) -o $@ main.cpp -L. -lcaesar -Wl,-rpath,.

install: libcaesar.so
	sudo cp $< /usr/local/lib/ && sudo ldconfig

test: caesar_cli
	@echo "Generating test files..."
	rm -rf test_in test_out log.txt
	mkdir -p test_in
	for i in $$(seq 1 12); do dd if=/dev/urandom of=test_in/file$$i.bin bs=1024 count=1 2>/dev/null; done
	
	@echo "Running Auto Mode (should be Parallel >= 5 files)..."
	./caesar_cli --mode=auto test_in 42 test_out
	
	@echo "Checking results..."
	ls test_out | wc -l | grep -q "12" && echo "All files processed OK" || echo "ERROR: Missing files"
	
	@echo "Cleaning up..."
	rm -rf test_in test_out log.txt

clean:
	rm -f libcaesar.so caesar_cli
	rm -rf test_in test_out log.txt

.PHONY: all install test clean