CXXFLAGS= -Os -fomit-frame-pointer
override CXXFLAGS+= -Wall -fsigned-char

default: all

all: vvm

clean:
	-$(RM) vvm

vvm:
	$(CXX) $(CXXFLAGS) -o vvm vvm.cpp
