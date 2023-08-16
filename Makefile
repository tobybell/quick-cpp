MODULES = quick
OBJECTS = $(patsubst %,build/%.o,$(MODULES))

SYSROOT = /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
CFLAGS = -Wall -Wconversion -Wunused-parameter -isysroot $(SYSROOT) -std=c++20 -g

all: build/quick

run: build/quick
	$<

build/quick: $(OBJECTS)
	clang++ -o $@ $(CFLAGS) -MD $^

build/%.o: %.cc
	clang++ -o $@ $(CFLAGS) -MD -c $<

-include $(OBJECTS:.o=.d)
