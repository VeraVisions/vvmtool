CXXFLAGS= -Os -fomit-frame-pointer
override CXXFLAGS+= -Wall -fsigned-char

default: all

all: vvm

clean:
	-$(RM) vvm vvm.o
	
%.o : %.cpp
	$(CXX) -o $@ -c $<

vvm: vvm.o
	$(CXX) -o vvm $^

vvm.o: vvm.cpp vvm.h util.h
