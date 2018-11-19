# ESP8266_Telemetry

A telemetry project to send data from a mobile device via ESP8266 to a host (e.g. Raspberry Pi).
The ESP8266 module used for this project is a WeMos D1.

This project is designed for lap counting on RC race tracks and to provide telemetry data from the vehicles.

#### Telemetry Host
The host sends a UDP broadcast package at a regular time interval (e.g. 10s) to facilitate automatic host address discovery by the client.
The broadcast data sent by the host consists of a single string with the following format:
> "LC1\t2006\tHOSTNAME"
- \t = tab character as separator.
- LC1 = Identifier. Future version may use LC2, LC3, etc.
- 2006 = Port number where the host is listening for telemetry packets
- HOSTNAME = the name of the host, for information only

##### Broadcast interval
It is suggested to broadcast every 10 seconds so a new station can begine sending telemetry within 10s of coming online.

#### Client Functionality
Client startup sequence:
- Establish WiFi access (LED flashes 250ms)
- Wait for Telemetry host broadcast (LED flashes 1s)
- Continually send telemetry packets to host

##### Telemetry packet structure
>0-5 client ID (MAC address)

>6   packet sequence number

>7   packet type

Data from byte 8 onwards depends on the packet type:

**Packet Type 0:** Keep alive.
Has no extra content, just type and sequence number. Sent at regular intervals. Not required when telemtry data is sent.

**Packet Type 1:** Lap Count event.
>8-11 time in ms since lap count signal has occured (long format).

This packet receives an acknowledgement from the telemetry host and is retransmitted if the host doesn't reply within 3 seconds
Each Lap Count packet is acknowledged by the host with a UDP packet to the client on the same port as the telemetry host port:
>0 - ACK (0x06) or NAK (0x15).
>1 - sequence number.

**Packet Type 2:** Telemetry data
>8 - Number of data items

>9 - Data type, Bit 7 = 4 bytes (high) or 2 bytes (low)

>y1 - data - depends on data type in prev byte, 1, 2 or 4 bytes long

>x2 - data type

>y2 - data

>x3 - data type

>y3 - data

>. - continues for all data items
