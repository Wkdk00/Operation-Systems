CC = g++
CFLAGS = -Wall -Wextra -pedantic -fPIC -shared -pthread

all: libcaesar.so

libcaesar.so: libcaesar.cpp
	$(CC) $(CFLAGS) -o $@ $<

install: libcaesar.so
	sudo cp $< /usr/local/lib/ && sudo ldconfig

test: libcaesar.so
	python3 test.py file1.bin file2.bin 42 ./out
	python3 test.py ./out/file1.bin ./out/file2.bin 42 ./restored
	
	cmp file1.bin ./restored/file1.bin && echo "file1 OK"
	cmp file2.bin ./restored/file2.bin && echo "file2 OK"

	rm -rf out restored log.txt

	python3 test.py * 42 /tmp/out
	python3 test.py /tmp/out/* 42 /tmp/out2
	md5sum /tmp/out/* * | sort 
	echo "===="
	md5sum /tmp/out2/* * | sort

clean:
	rm -f libcaesar.so
	rm -rf out restored file1.bin file2.bin

.PHONY: all install test clean