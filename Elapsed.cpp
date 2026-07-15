#include "Elapsed.h"

bool elapsedWithin(uint32_t now, uint32_t then, uint32_t maxWindow, uint32_t *elapsedOut) {
  uint32_t gap = now - then; // wraparound-correct forward distance mod 2^32
  if (gap > maxWindow) {
    return false;
  }
  *elapsedOut = gap;
  return true;
}

uint32_t saturatingAddMs(uint32_t a, uint32_t b) {
  uint32_t sum = a + b;
  if (sum < a) { // wrapped past UINT32_MAX
    return UINT32_MAX;
  }
  return sum;
}
