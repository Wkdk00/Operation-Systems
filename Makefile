CC = g++
CFLAGS = -Wall -Wextra -pedantic -fPIC -shared -pthread

all: libcaesar.so caesar_cli

libcaesar.so: libcaesar.cpp
	$(CC) $(CFLAGS) -o $@ $<

caesar_cli: main.cpp libcaesar.so
	$(CC) -Wall -Wextra -pedantic -pthread -std=c++17 -o $@ main.cpp -L. -lcaesar -Wl,-rpath,.

install: libcaesar.so
	sudo cp $< /usr/local/lib/ && sudo ldconfig

test: caesar_cli
	# Создаём тестовые файлы
	dd if=/dev/urandom of=file1.bin bs=1024 count=1 2>/dev/null
	dd if=/dev/urandom of=file2.bin bs=1024 count=2 2>/dev/null

	# Шифрование
	./caesar_cli file1.bin file2.bin 42 ./out
	# Дешифрование
	./caesar_cli ./out/file1.bin ./out/file2.bin 42 ./restored

	# Проверка
	cmp file1.bin ./restored/file1.bin && echo "file1 OK"
	cmp file2.bin ./restored/file2.bin && echo "file2 OK"

	rm -rf out restored log.txt file1.bin file2.bin

clean:
	rm -f libcaesar.so caesar_cli
	rm -rf out restored file1.bin file2.bin log.txt /tmp/out /tmp/out2 /tmp/file1.bin /tmp/file2.bin

.PHONY: all install test clean