#include "BootloaderMagic.h"

const char BootloaderMagic::magic_[] = "rebootnow";

bool BootloaderMagic::feed(char c) {
  if(c == magic_[matched_]) {
    matched_++;
    if(magic_[matched_] == '\0') {
      matched_ = 0;
      return true;
    }
    return false;
  }
  matched_ = (c == magic_[0]) ? 1 : 0;
  return false;
}
