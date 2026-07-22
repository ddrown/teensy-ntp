#!/bin/sh

SERIAL=7665320
PORT=/dev/serial/by-id/usb-Teensyduino_USB_Serial_${SERIAL}-if00
if [ -c "$PORT" ]; then
  tycmd monitor -B $SERIAL
else
  echo no port
fi
