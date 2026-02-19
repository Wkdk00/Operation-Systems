CC = g++
CFLAGS = -Wall -Wextra -pedantic -fPIC -shared

all: libcaesar.so

libcaesar.so: libcaesar.cpp
	$(CC) $(CFLAGS) -o $@ $<

install: libcaesar.so
	sudo cp $< /usr/local/lib/ && sudo ldconfig

test: libcaesar.so
	python3 test.py ./libcaesar.so 42 input.bin output.bin
	python3 test.py ./libcaesar.so 42 output.bin restored.bin
	cmp input.bin restored.bin

clean:
	rm -f libcaesar.so output.bin restored.bin

.PHONY: all install test clean