Hardware:
* teensy 4.1 + ethernet connector
* GPS module with PPS on pin 35, GPS TX (pin 0), GPS RX (pin 1) (I'm using a ublox Neo-7N, but many others would work)

Software:
* put https://github.com/ddrown/teensy41_ethernet in your Arduino libraries folder

For the GPS module, you just need PPS, serial output at 115200, and ZDA NMEA messages enabled.  If you need a different GPS serial speed or need to use the RMC NMEA message, see the `settings.h` file.

For tests, see the `test/` directory.  It uses the ArduinoFake mock environment, which uses the FakeIt mock system and Unity unit tests.

For NTP+GPS status, go to http://[ip]

It will start generating a graph of the local Clock Offset, the local Clock Sync parameters, the Satellite Signal strength, and a "radar" view of the GPS satellites overhead.

![](https://raw.githubusercontent.com/wiki/ddrown/teensy-ntp/graph1.png)
