#include "Elapsed.h"

bool elapsedWithin(uint32_t now, uint32_t then, uint32_t maxWindow, uint32_t *elapsedOut) {
  uint32_t gap = now - then; // wraparound-correct forward distance mod 2^32
  if (gap > maxWindow) {
    return false;
  }
  *elapsedOut = gap;
  return true;
}
