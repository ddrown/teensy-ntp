settings are in settings.h-example, make a copy of it to settings.h and add your SSID

Hardware:
* esp8266 (I'm using an Adafruit Huzzah, but others esp8266's would work)
* GPS module with PPS on pin 5, GPS RX pin 15, GPS TX pin 13 (I'm using a ublox Neo-7N, but many others would work)

Log messages:
* softwareserial logs are on TX 1/RX 3 to the USB serial port
* text logs sent over UDP to logDestinationIP:51413, you can receive them with nc or socat

For the GPS module, you just need PPS, serial output at 115200, and ZDA NMEA messages enabled.  If you need a different GPS serial speed, adjust `BAUD_SERIAL`

For tests, see the `test/` directory.  It uses the ArduinoFake mock environment, which uses the FakeIt mock system and Unity unit tests.
