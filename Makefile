CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

TARGET = libcaesar.so
SRC = libcaesar.c

all: $(TARGET) librc4.so

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

librc4.so: librc4.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o librc4.so librc4.c

install: $(TARGET) librc4.so
	sudo cp $(TARGET) librc4.so /usr/local/lib
	sudo ldconfig

test: $(TARGET)
	python3 test.py

clean:
	rm -f $(TARGET) librc4.so secure_copy

compile:
	gcc secure_copy.c -o secure_copy -pthread -lcaesar -lrc4 -L. -Wl,-rpath,. -Wall -Wextra

test_seq:
	./secure_copy --mode=sequential f6.bin f7.bin outdir1 12

test_par:
	./secure_copy --mode=parallel f6.bin f7.bin outdir1 12

test_auto:
	./secure_copy f6.bin f7.bin outdir1 12