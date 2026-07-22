#!/bin/sh

PORT=/dev/serial/by-id/usb-Teensyduino_USB_Serial_7665320-if00
if [ -c "$PORT" ]; then
  echo rebootnow >"$PORT"
fi
tycmd upload -B 7665320 --wait build/teensy.avr.teensy41/teensy-ntp.ino.hex
