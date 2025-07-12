# Bluetooth hardware interface (C / bluetoothctl)

The module runs `bluetoothctl --timeout <seconds> scan on`, parses `Device` and `RSSI` events, and succeeds only when the configured target name is found with RSSI >= configured threshold.

Current test command:

```sh
./bluetooth_test Mate40 10000 -80
```

The module first requires `bluetoothctl show` to report a default controller. No controller is currently visible on the 3576 device, so a real scan requires the Bluetooth hardware/driver to be enabled first.
