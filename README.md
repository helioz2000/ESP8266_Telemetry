# ESP8266_Telemetry

A telemetry project to send data from a mobile device via ESP8266 to a host (e.g. Raspberry Pi)
The ESP8266 module used for this project is a WeMos D1

This project is designed for RC cars lap counting and to provide telemetry data from the cars.

#### Telemetry Host
The host sends a UDP broadcast package at a regular time interval (e.g. 10s) to facilitate automatic host address discovery by the client.
The data sent by the host is a string with the following format: "LC1\t2006\thostname" with \t being a tab character separator.
- LC1 = Identifier. Future version may use LC2, LC3, etc.
- 2006 = Port number where the host is listening for telemetry packets
- hostname = the name of the host, for information only

##### Broadcast interval
It is suggested to broadcast every 10 seconds so a new station can begine sending telemetry within 10s of coming online.

