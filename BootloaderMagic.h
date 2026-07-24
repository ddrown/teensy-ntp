#pragma once

#include <stddef.h>

// Watches an incoming character stream for the literal string "rebootnow"
// (typed over the same serial connection normal firmware output uses, e.g.
// by teensy_loader_cli) as a software-triggered reboot into the bootloader.
// Pure state machine, no I/O of its own, so it's host-testable without
// mocking Serial at all -- see bootloader_poll() in teensy-ntp.ino for the
// actual reboot side effect this triggers.
class BootloaderMagic {
  public:
    BootloaderMagic(): matched_(0) {};

    // Feed the next received character; returns true exactly once, the
    // instant the full magic string has just been matched (the caller is
    // then responsible for actually rebooting). Resets internal state after
    // a match so the same instance could in principle match again -- moot in
    // production, since the real caller reboots immediately, but the
    // correct, unsurprising behavior for a reusable matcher.
    bool feed(char c);

  private:
    static const char magic_[];
    size_t matched_;
};
