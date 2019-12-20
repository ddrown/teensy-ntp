ARDUINOFAKEDIR=../../ArduinoFake
INCLUDES=-I$(ARDUINOFAKEDIR)/src/ -I$(ARDUINOFAKEDIR)/external/unity/unity-repo/src/ -I..
CXX_FLAGS = -Wfatal-errors -Wall -Wshadow

TESTSRC=$(wildcard *.cpp)
APPSRC=$(wildcard ../*.cpp)
SRCS=$(TESTSRC) $(APPSRC)
OBJ=$(TESTSRC:%.cpp=%.o) $(APPSRC:../%.cpp=%.o)
DEP=$(OBJ:%.o=%.d)

LIBS=$(ARDUINOFAKEDIR)/build/src/libArduinoFake.so $(ARDUINOFAKEDIR)/build/external/unity/libunity.a

all: test
	LD_LIBRARY_PATH=$(ARDUINOFAKEDIR)/build/src/ ./test

clean:
	rm -f *.o test .depend

dep: .depend

.depend: $(SRCS)
	g++ $(INCLUDES) -MM $^ > ./.depend

include .depend

test: $(OBJ) $(LIBS)
	g++ -o $@ $^

test.o: test.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

test%.o: test%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

%.o: ../%.cpp
	g++ -c $(INCLUDES) $(CXX_FLAGS) $<

.PHONY: clean all dep
