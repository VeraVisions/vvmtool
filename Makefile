CXXFLAGS= -Os -fomit-frame-pointer
override CXXFLAGS+= -Wall -fsigned-char

default: all

all: vvm

clean:
	-$(RM) vvm vvm.o

vvm: vvm.o
	$(CXX) $(CXXFLAGS) -o vvm vvm.o
vvm.o:
	$(CXX) -o vvm.o -c vvm.cpp
