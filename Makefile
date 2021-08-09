CXXFLAGS= -Os -fomit-frame-pointer
override CXXFLAGS+= -Wall -fsigned-char

default: all

all: vvmtool

clean:
	-$(RM) vvmtool vvm.o
	
%.o : %.cpp
	$(CXX) -o $@ -c $<

vvmtool: vvm.o
	$(CXX) -o vvmtool $^

vvm.o: vvm.cpp vvm.h util.h
