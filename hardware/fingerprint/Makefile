CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: fingerprint.o fingerprint_test

fingerprint.o: fingerprint.c fingerprint.h
	$(CC) $(CFLAGS) -c fingerprint.c -o $@

fingerprint_test: fingerprint_test.c fingerprint.o fingerprint.h
	$(CC) $(CFLAGS) fingerprint_test.c fingerprint.o -o $@

clean:
	rm -f fingerprint.o fingerprint_test
