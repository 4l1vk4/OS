CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

TARGET = libcaesar.so
SRC = libcaesar.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/lib
	sudo ldconfig

test: $(TARGET)
	python3 test.py

clean:
	rm -f $(TARGET)
compile:
	gcc secure_copy.c -o secure_copy -pthread -lcaesar -L. -Wall -Wextra
test_seq:
	./secure_copy --mode=sequential f6.bin f7.bin outdir1 12
test_par:
	./secure_copy --mode=parallel f6.bin f7.bin outdir1 12
test_auto:
	./secure_copy f6.bin f7.bin outdir1 12