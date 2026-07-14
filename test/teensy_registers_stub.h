#pragma once

// Minimal stand-ins for the Teensy 4.1 ENET/IOMUX registers and interrupt
// helpers that InputCapture.cpp normally gets transitively from the real
// Teensyduino core headers. ArduinoFake doesn't provide these, so this file
// is force-included (see test/Makefile) only when building InputCapture.o
// for the host test suite -- it never touches the real firmware build.

#include <stdint.h>

#define IRQ_ENET_TIMER 0

static uint32_t ENET_TCSR0 = 0;
static uint32_t ENET_TCCR0 = 0;
static uint32_t IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_12 = 0;
static uint32_t IOMUXC_ENET0_TIMER_SELECT_INPUT = 0;

static inline void attachInterruptVector(int irq, void (*handler)()) {}
static inline void NVIC_ENABLE_IRQ(int irq) {}
