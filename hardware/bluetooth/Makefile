CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

all: bluetoothctl_scan.o bluetooth_test

bluetoothctl_scan.o: bluetoothctl_scan.c bluetoothctl_scan.h
	$(CC) $(CFLAGS) -c bluetoothctl_scan.c -o $@

bluetooth_test: bluetooth_test.c bluetoothctl_scan.o bluetoothctl_scan.h
	$(CC) $(CFLAGS) bluetooth_test.c bluetoothctl_scan.o -o $@

clean:
	rm -f bluetoothctl_scan.o bluetooth_test
