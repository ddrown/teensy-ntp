ARDUINOFAKEDIR=../../ArduinoFake
INCLUDES=-I$(ARDUINOFAKEDIR)/src/ -I$(ARDUINOFAKEDIR)/external/unity/unity-repo/src/ -I..
CXX_FLAGS = -Wfatal-errors -Wall -Wshadow -DCOUNTERFUNC=micros -DCOUNTSPERSECOND=1000000 -DNTPPID_KP=0.001 -DNTPPID_KI=0.001 -DNTPPID_KD=1.0

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

test-DateTime: test-DateTime.o DateTime.o $(LIBS)
test-GPS: test-GPS.o DateTime.o GPS.o $(LIBS)
test-NTPClock: test-NTPClock.o NTPClock.o $(LIBS)
test-ClockPID: test-ClockPID.o ClockPID.o $(LIBS)

$(BINS):
	g++ $(CXX_FLAGS) -o $@ $^

test%.o: test%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

%.o: ../%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

.PHONY: clean all dep
