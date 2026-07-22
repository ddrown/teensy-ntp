# Teensy 4.1 GPS/PPS NTP server

A GPS-disciplined NTP server that runs entirely on a Teensy 4.1 + Ethernet, built around
hardware timestamping end to end rather than a general-purpose OS scheduler: GPS PPS
(pulse-per-second) is captured as a hardware timestamp from a timer input capture pin (not a
software/`millis()` edge poll), and every NTP packet the server receives or sends is
hardware-timestamped by the same onboard 1588 timer at the MAC layer, not timestamped in
software after the fact -- see "Hardware timestamping" below. GPS NMEA sentences (ZDA, or
RMC+GGA) provide the calendar time, and a Theil-Sen regression + chi-squared discipline loop
(not a naive PI loop) steers the 1588 counter.

This is deliberately not the common Raspberry Pi + gpsd + chrony approach, where hardware
timestamping is largely absent. A PPS-disciplined RPi setup uses the kernel's GPIO PPS driver,
but that's still a *software* timestamp -- taken by the interrupt handler reading the system
clock at the moment it runs, subject to interrupt latency/scheduling jitter, not a timer
peripheral latching the edge in hardware. Before the Pi 5, the built-in NICs don't do hardware
PTP timestamping either, so the NTP request/response packets chrony actually serves are also
software-timestamped after crossing the OS network stack. The Pi 5 can hardware timestamp NTP
packets, but that only reduces some of the error. Most setup instructions I've seen for a
Raspberry Pi NTP server do not turn on hardware timestamps. This design hardware-timestamps the
whole chain instead -- the PPS edge and every served packet alike -- with no OS interrupt
latency or scheduling jitter in the loop. It's also cheaper, lower power, and has no SD card
(or other storage) to corrupt on power loss.

A few other things that set this apart from a typical GPS-disciplined NTP server:

- Theil-Sen linear regression + chi-squared goodness-of-fit for drift-rate estimation, instead
  of a naive PI loop
- Compiled-in leap-second table with a TAI-like internal time domain -- steps rather than
  smears, no reliance on the network for leap-second announcements
- Host-side unit test coverage (ArduinoFake/Unity) for an Arduino sketch -- most of the
  firmware's logic is exercised without needing real hardware

## Hardware

| Signal | Teensy 4.1 pin | Notes |
|---|---|---|
| GPS PPS | 35 | Hardcoded to the 1588 timer's dedicated event-capture input (`InputCapture.cpp`); rising edge |
| GPS TX | 0 (Teensy RX1) | `GPS_SERIAL` in `settings.h` |
| GPS RX | 1 (Teensy TX1) | wired for completeness; unused by the firmware today (no commands are sent to the GPS module) |
| Ethernet | built-in RJ45 jack | Teensy 4.1's onboard PHY, no external wiring needed |
| Power | 5V/GND or USB | depends on the GPS module |

Any GPS module that outputs standard NMEA (ZDA, or RMC+GGA) with a PPS signal should work --
this was built and tested against a u-blox Neo-7N, but nothing in the firmware is Neo-7N-specific.
Sentence type (ZDA vs. RMC+GGA) and arrival order are detected at runtime (`GPS.cpp`), not a
compile-time choice.

## Hardware timestamping

Every timestamp this server's accuracy actually depends on is captured in hardware, not
software:

- **PPS input capture** -- pin 35's edge is captured directly by the 1588 timer's dedicated
  capture channel (`InputCapture.cpp`), not polled or timestamped in software.
- **Incoming NTP packet receive time** -- the ENET MAC hardware timestamps each packet as it
  arrives (`lwip_t41.c`: `p->timestamp = bdPtr->timestamp`), before software ever sees it.
- **Outgoing NTP packet transmit time** -- likewise timestamped by the MAC hardware once the
  packet actually leaves the wire (`enet_txTimestampNextPacket()`/
  `enet_set_tx_timestamp_callback()`), not estimated at the moment `udp_sendto()` is called.

The transmit timestamp isn't available synchronously: the hardware only knows it after the
packet has actually gone out, which is after that response has already been built and sent.
**NTP interleaved mode** exists specifically to deliver that real value back to the client --
the first response to a given client uses a software-estimated transmit time (`NTPServer.cpp`'s
"basic mode" path), but the *next* request from that same client gets the real hardware-captured
transmit timestamp from the previous exchange instead (`NTPClients` tracks the pending value per
client). Clients that support interleaved mode (chrony) get the more precise value
automatically; clients that don't still get a correct, just less precise, basic-mode response.

## Accuracy in practice

The NTP `precision` field this server reports is -24 (about 40ns, from the 25MHz 1588 counter
tick), but that's a quantization floor, not the achieved accuracy. In bench testing -- two
Teensys, each with its own GPS/PPS, compared against each other via a real NTP client -- served
time agreed to within a few hundred nanoseconds, well inside the tens-of-microseconds maximum error
bounds of the comparison setup itself.

## GPS/PPS loss (holdover)

If GPS/PPS goes quiet for `HOLDOVER_STALE_MS` (4 seconds, `ClockHoldover.h`), the server doesn't
just stop -- it enters holdover:

- Discipline switches from the full PID output to just the drift-rate (D) term, since the
  phase/accumulated-offset terms go stale without fresh samples.
- The reported dispersion estimate keeps growing continuously at `HOLDOVER_PHI_PPM` (15ppm, a
  worst-case oscillator drift-rate bound), rather than freezing at its last real value.
- NTP responses keep being served throughout, with the growing dispersion visible to clients.
- After roughly 18.5 hours of continuous outage, dispersion crosses NTP's 1-second reporting
  threshold and the server falls back to stratum 16 / leap-indicator "unsynchronized" --
  telling clients to stop trusting it, rather than serving indefinitely-stale time forever.
- Reconnecting GPS/PPS clears holdover on the next accepted sample, with no clock step.

See `TESTPLAN.md` section 4 for how to exercise this by hand (pull the PPS line).

## Building and flashing

### Arduino IDE + Teensyduino

* put https://github.com/ddrown/teensy41_ethernet in your Arduino libraries folder
* install [Teensyduino](https://www.pjrc.com/teensy/td_download.html) (adds Teensy board
  support to the Arduino IDE)
* **Tools > Board > Teensyduino > Teensy 4.1**, open `teensy-ntp.ino`, compile and upload as
  normal

### arduino-cli (no IDE)

PJRC also publishes a Boards Manager-compatible package index, so the compiler, uploader, and
`teensy:avr` core can be installed without the IDE:
```
arduino-cli config add board_manager.additional_urls https://www.pjrc.com/teensy/package_teensy_index.json
arduino-cli core update-index
arduino-cli core install teensy:avr
```
`teensy41_ethernet` still needs to be `git clone`d directly into arduino-cli's user libraries
directory (it isn't in the Library Manager index). `compile.sh`/`upload.sh`/`monitor.sh` in this
repo wrap the resulting `arduino-cli`/[tycmd](https://github.com/Koromix/tytools) invocations.

For the GPS module itself, you just need PPS, NMEA serial output, and ZDA (or RMC+GGA) messages
enabled -- see `settings.h` options below for baud rate and other per-device configuration.

## `settings.h` options

| Define | Meaning |
|---|---|
| `GPS_BAUD` | GPS module's NMEA serial baud rate. Many u-blox modules ship at 9600 and need reconfiguring (e.g. via u-center) to run faster; this must match whatever the module is actually configured for, or nothing will decode (see Troubleshooting) |
| `GPS_SERIAL` | Which Teensy hardware UART the GPS module is wired to (`Serial1` by default) |
| `hostnameTable[]` | Maps each board's MAC address to a DHCP hostname, so the same compiled firmware image can be flashed onto multiple boards -- see `hostnameForMac()` in `teensy-ntp.ino`. Boards not listed fall back to a generated `teensy-xxxxxx` name from their MAC. Boot once, read the MAC printed over serial, add an entry, reflash |

## Web status page

For NTP+GPS status, go to http://[ip]

It will start generating a graph of the local Clock Offset, the local Clock Sync parameters, the Satellite Signal strength, and a "radar" view of the GPS satellites overhead.

![](https://raw.githubusercontent.com/wiki/ddrown/teensy-ntp/graph1.png)

## Troubleshooting

- **No satellites/no signal at all** (not just slow to lock): check `GPS_BAUD` actually matches
  the module's configured baud rate first. A baud mismatch produces no valid NMEA at all rather
  than an obvious error -- `GPS.cpp` verifies each sentence's checksum and silently drops
  anything that fails, with no dedicated log line, so a wrong baud rate looks identical to "no
  satellites in view" from the firmware's point of view. Tap the raw UART with a USB-serial
  adapter to confirm the module is actually decoding at the baud rate you think it is.
- **GPS won't lock / slow to lock**: a cold start with no cached almanac can take up to ~12.5
  minutes; satellite visibility also depends heavily on antenna placement/sky view. You should
  see satellites showing up in the signal strength graph while the almanac is being downloaded.
- **No PPS detected**: watch for `LAG <ppsToGPS> <ppsMillis> <gpstime>` on the serial console
  (see `TESTPLAN.md` section 3 for the full message reference) and confirm holdover engages
  (`inHoldover` in the web/JSON status) rather than nothing happening. Check the PPS wiring to
  pin 35 specifically.

## Testing

For host-side unit tests, see the `test/` directory (`ArduinoFake`/`FakeIt` mocks + Unity). The
full bench verification checklist (dual-Teensy comparison, holdover, leap-second handling,
Y2036 wraparound) is in `TESTPLAN.md`. Project status/history: open work in `TODO.md`, completed
work with implementation notes in `DONE.md`.

## Memory usage

IPv4 only:
```
Memory Usage on Teensy 4.1:
  FLASH: code:95992, data:18708, headers:9200   free for files:8002564
   RAM1: variables:88000, code:93432, padding:4872   free for local variables:337984
   RAM2: variables:12416  free for malloc/new:511872

```

IPv4+IPv6:
```
Memory Usage on Teensy 4.1:
  FLASH: code:113208, data:19732, headers:8368   free for files:7985156
   RAM1: variables:92864, code:110648, padding:20424   free for local variables:300352
   RAM2: variables:12416  free for malloc/new:511872
```

## License

MIT -- see `LICENSE`.
