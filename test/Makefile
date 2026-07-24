ARDUINOFAKEDIR=../../ArduinoFake
INCLUDES=-I$(ARDUINOFAKEDIR)/src/ -I$(ARDUINOFAKEDIR)/external/unity/unity-repo/src/ -I.. -I.
CXX_FLAGS = -Wfatal-errors -Wall -Wshadow -DCOUNTERFUNC=micros -DCOUNTSPERSECOND=1000000 -DNTPPID_KP=0.001 -DNTPPID_KI=0.001 -DNTPPID_KD=1.0 -g

TESTSRC=$(wildcard *.cpp)
APPSRC=$(wildcard ../*.cpp)
SRCS=$(TESTSRC) $(APPSRC)
OBJ=$(APPSRC:../%.cpp=%.o)
BINS=$(TESTSRC:%.cpp=%)
DEP=$(OBJ:%.o=%.d)

LIBS=$(ARDUINOFAKEDIR)/build/src/libArduinoFake.so $(ARDUINOFAKEDIR)/build/external/unity/libunity.a

all: $(BINS)
	echo ----------------------------------------------------------------
	for i in $(BINS); do LD_LIBRARY_PATH=$(ARDUINOFAKEDIR)/build/src/ ./$$i; done

run-test-%: test-%
	LD_LIBRARY_PATH=$(ARDUINOFAKEDIR)/build/src/ ./$^

clean:
	rm -f *.o $(BINS) .depend

dep: $(SRCS)
	g++ $(INCLUDES) -MM $^ > ./.depend

-include .depend

test-DateTime: test-DateTime.o DateTime.o LeapSeconds.o $(LIBS)
test-GPS: test-GPS.o DateTime.o LeapSeconds.o GPS.o $(LIBS)
test-NTPClock: test-NTPClock.o NTPClock.o $(LIBS)
test-ClockPID: test-ClockPID.o ClockPID.o $(LIBS)
test-ClockDiscipline: test-ClockDiscipline.o ClockDiscipline.o ClockPID.o NTPClock.o LeapSeconds.o Elapsed.o $(LIBS)
test-Elapsed: test-Elapsed.o Elapsed.o $(LIBS)
test-ClockHoldover: test-ClockHoldover.o ClockHoldover.o Elapsed.o ClockPID.o NTPClock.o $(LIBS)
test-LeapSeconds: test-LeapSeconds.o LeapSeconds.o DateTime.o $(LIBS)
test-InputCapture: test-InputCapture.o InputCapture.o $(LIBS)
test-NTPClients: test-NTPClients.o NTPClients.o NTPClock.o LeapSeconds.o Elapsed.o $(LIBS)
test-NTPResponseFields: test-NTPResponseFields.o NTPResponseFields.o LeapSeconds.o $(LIBS)
test-Hostname: test-Hostname.o Hostname.o $(LIBS)
test-BootloaderMagic: test-BootloaderMagic.o BootloaderMagic.o $(LIBS)
test-GpsBaud: test-GpsBaud.o DateTime.o LeapSeconds.o GPS.o $(LIBS)
test-UpdateTimeCore: test-UpdateTimeCore.o UpdateTimeCore.o ClockDiscipline.o ClockHoldover.o ClockPID.o NTPClock.o LeapSeconds.o Elapsed.o $(LIBS)

$(BINS):
	g++ $(CXX_FLAGS) -o $@ $^

test%.o: test%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

# InputCapture.cpp normally relies on real Teensyduino core headers for
# ENET/IOMUX registers, which ArduinoFake doesn't provide; force-include a
# test-only stub for them (see teensy_registers_stub.h) rather than teaching
# the real firmware code about the test environment.
InputCapture.o: ../InputCapture.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) -include teensy_registers_stub.h $<

%.o: ../%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

.PHONY: clean all dep
