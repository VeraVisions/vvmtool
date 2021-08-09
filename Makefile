CXXFLAGS= -Os -fomit-frame-pointer
override CXXFLAGS+= -Wall -fsigned-char

VVM_OBJS= \
	vvm.o
UPGRADE_OBJS= \
	upgrade.o

default: all

all: vvm #upgrade

clean:
	-$(RM) $(VVM_OBJS) $(UPGRADE_OBJS) vvm upgrade

vvm: $(VVM_OBJS)
	$(CXX) $(CXXFLAGS) -o vvm $(VVM_OBJS)
upgrade: $(UPGRADE_OBJS)
	$(CXX) $(CXXFLAGS) -o upgrade $(UPGRADE_OBJS)

%.o : %.cpp
	$(CXX) -o $@ -c $<
