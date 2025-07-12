CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: wifi_nmcli.o wifi_test

wifi_nmcli.o: wifi_nmcli.c wifi_nmcli.h
	$(CC) $(CFLAGS) -c wifi_nmcli.c -o $@

wifi_test: wifi_test.c wifi_nmcli.o wifi_nmcli.h
	$(CC) $(CFLAGS) wifi_test.c wifi_nmcli.o -o $@

clean:
	rm -f wifi_nmcli.o wifi_test
