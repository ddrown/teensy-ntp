#!/bin/sh

arduino-cli compile -b teensy:avr:teensy41 -ev --build-property "build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160 -DLWIP_IPV6=1"
