CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

TARGET = libcaesar.so
SRC = libcaesar.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	sudo cp $(TARGET) /test
	sudo ldconfig

test: $(TARGET)
	python3 test.py

clean:
	rm -f $(TARGET)