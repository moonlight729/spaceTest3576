# Wi-Fi hardware interface (C / nmcli)

This module owns only the device-side Wi-Fi operations:

1. Detect a Wi-Fi interface with `nmcli -t -f DEVICE,TYPE device status`.
2. Connect with `nmcli device wifi connect` using the caller supplied SSID/password.
3. Obtain the DHCP IPv4 address with `nmcli -g IP4.ADDRESS device show`.
4. Ping the upper-computer supplied `routerIp` and return average delay.

The `manage` layer must send the protocol stages `connecting`, `pinging`, then final `passed`/`failed`. No credential, SSID, router IP or timeout is compiled into this module.

`nmcli` may require NetworkManager permissions for the service account. Verify `nmcli device status` and the required polkit policy before hardware testing.
