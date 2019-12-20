ARDUINOFAKEDIR=../../ArduinoFake
INCLUDES=-I$(ARDUINOFAKEDIR)/src/ -I$(ARDUINOFAKEDIR)/external/unity/unity-repo/src/ -I..
CXX_FLAGS = -Wfatal-errors -Wall -Wshadow

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

clean:
	rm -f *.o test .depend

dep: $(SRCS)
	g++ $(INCLUDES) -MM $^ > ./.depend

-include .depend

test-DateTime: test-DateTime.o $(OBJ) $(LIBS)
test-GPS: test-GPS.o $(OBJ) $(LIBS)
test-NTPClock: test-NTPClock.o $(OBJ) $(LIBS)
test-ClockPID: test-ClockPID.o $(OBJ) $(LIBS)

$(BINS):
	g++ $(CXX_FLAGS) -o $@ $^

test%.o: test%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

%.o: ../%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

.PHONY: clean all dep
