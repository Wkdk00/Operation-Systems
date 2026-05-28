CC = g++
CFLAGS_LIB = -Wall -Wextra -pedantic -fPIC -shared
CFLAGS_BIN = -Wall -Wextra -pedantic -pthread -std=c++17 -DWORKERS_COUNT=5

all: librc4.so secure_copy

librc4.so: librc4.cpp
	$(CC) $(CFLAGS_LIB) -o $@ $<

secure_copy: main.cpp librc4.so
	$(CC) $(CFLAGS_BIN) -o $@ main.cpp -L. -lrc4 -Wl,-rpath,.

install: librc4.so
	sudo cp $< /usr/local/lib/ && sudo ldconfig

test: secure_copy
	@rm -rf t_in t_out t.img; mkdir -p t_in t_out
	@for i in 1 2 3; do echo "test$$i" > t_in/f$$i.txt; done
	@./secure_copy -add -key "k" -image t.img t_in/
	@for i in 1 2 3; do ./secure_copy -get -image t.img -key "k" -out t_out/f$$i.txt t_in/f$$i.txt 2>/dev/null; done
	@cmp -s t_in/f1.txt t_out/f1.txt && cmp -s t_in/f2.txt t_out/f2.txt && cmp -s t_in/f3.txt t_out/f3.txt && echo "✓ Tests passed" || echo "✗ Tests failed"
	@rm -rf t_in t_out t.img

clean:
	rm -f librc4.so secure_copy
	rm -rf disk.img out result_file test_in test_out test_image.img

.PHONY: all install test clean