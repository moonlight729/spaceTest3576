CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: fast_charge.o fast_charge_test
fast_charge.o: fast_charge.c fast_charge.h
	$(CC) $(CFLAGS) -c fast_charge.c -o $@
fast_charge_test: fast_charge_test.c fast_charge.o fast_charge.h
	$(CC) $(CFLAGS) fast_charge_test.c fast_charge.o -o $@
clean:
	rm -f fast_charge.o fast_charge_test
