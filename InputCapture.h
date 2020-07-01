#pragma once

class InputCapture {
  public:
    InputCapture();
    void newCapture(uint32_t count);
    void begin();
    uint32_t getCount() { return lastCount; };
    uint32_t getMillis() { return lastMillis; };
    uint32_t getCaptures() { return captures; };

  private:
    uint32_t lastCount;
    uint32_t lastMillis;
    uint32_t captures;
};

extern InputCapture pps;
