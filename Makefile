CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

APP_OBJECTS = \
	config/app_config.o \
	protocol/protocol.o \
	storage/board_state.o \
	hardware/fingerprint/fingerprint.o \
	hardware/bluetooth/bluetoothctl_scan.o \
	hardware/camera/camera_stream.o \
	hardware/fast_charge/fast_charge.o \
	hardware/keys/key_input.o \
	hardware/wifi/wifi_nmcli.o \
	hardware/tf_card/tf_card.o \
	tests/test_runner.o \
	manage/session_manager.o \
	main.o

all: spacetest3576

spacetest3576: $(APP_OBJECTS)
	$(CC) $(CFLAGS) $(APP_OBJECTS) -o $@

config/app_config.o: config/app_config.c config/app_config.h
	$(CC) $(CFLAGS) -c config/app_config.c -o $@
protocol/protocol.o: protocol/protocol.c protocol/protocol.h
	$(CC) $(CFLAGS) -c protocol/protocol.c -o $@
storage/board_state.o: storage/board_state.c storage/board_state.h
	$(CC) $(CFLAGS) -c storage/board_state.c -o $@
hardware/fingerprint/fingerprint.o: hardware/fingerprint/fingerprint.c hardware/fingerprint/fingerprint.h
	$(CC) $(CFLAGS) -c hardware/fingerprint/fingerprint.c -o $@
hardware/bluetooth/bluetoothctl_scan.o: hardware/bluetooth/bluetoothctl_scan.c hardware/bluetooth/bluetoothctl_scan.h
	$(CC) $(CFLAGS) -c hardware/bluetooth/bluetoothctl_scan.c -o $@
hardware/camera/camera_stream.o: hardware/camera/camera_stream.c hardware/camera/camera_stream.h
	$(CC) $(CFLAGS) -c hardware/camera/camera_stream.c -o $@
hardware/fast_charge/fast_charge.o: hardware/fast_charge/fast_charge.c hardware/fast_charge/fast_charge.h
	$(CC) $(CFLAGS) -c hardware/fast_charge/fast_charge.c -o $@
hardware/keys/key_input.o: hardware/keys/key_input.c hardware/keys/key_input.h
	$(CC) $(CFLAGS) -c hardware/keys/key_input.c -o $@
hardware/wifi/wifi_nmcli.o: hardware/wifi/wifi_nmcli.c hardware/wifi/wifi_nmcli.h
	$(CC) $(CFLAGS) -c hardware/wifi/wifi_nmcli.c -o $@
hardware/tf_card/tf_card.o: hardware/tf_card/tf_card.c hardware/tf_card/tf_card.h
	$(CC) $(CFLAGS) -c hardware/tf_card/tf_card.c -o $@
tests/test_runner.o: tests/test_runner.c tests/test_runner.h protocol/protocol.h storage/board_state.h hardware/fingerprint/fingerprint.h hardware/bluetooth/bluetoothctl_scan.h hardware/camera/camera_stream.h hardware/fast_charge/fast_charge.h hardware/keys/key_input.h hardware/wifi/wifi_nmcli.h hardware/tf_card/tf_card.h
	$(CC) $(CFLAGS) -c tests/test_runner.c -o $@
manage/session_manager.o: manage/session_manager.c manage/session_manager.h protocol/protocol.h storage/board_state.h tests/test_runner.h
	$(CC) $(CFLAGS) -c manage/session_manager.c -o $@
main.o: main.c config/app_config.h manage/session_manager.h
	$(CC) $(CFLAGS) -c main.c -o $@

clean:
	rm -f $(APP_OBJECTS) spacetest3576
