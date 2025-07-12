CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: key_input.o

key_input.o: key_input.c key_input.h
	$(CC) $(CFLAGS) -c key_input.c -o $@

clean:
	rm -f key_input.o
