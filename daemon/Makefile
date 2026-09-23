# platod - ARM side of the MiSTer PLATO core
#
#   make            native build (for testing with --sim)
#   make arm        cross build for MiSTer (runs in the plato-armhf container)

SRCS = src/platod.cpp src/engine.cpp src/net.cpp src/keymap.cpp src/audio.cpp pterm/Z80.cpp
CXXFLAGS = -std=c++11 -O2 -Wall -Wno-reorder -Wno-reorder-ctor -Wno-unknown-warning-option -Isrc -Ipterm

build/platod: $(SRCS) src/*.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

arm:
	docker run --rm -v "$(CURDIR)":/src -w /src plato-armhf \
	  make CXX=arm-linux-gnueabihf-g++ OUT=build-arm/platod build-arm/platod

build-arm/platod: $(SRCS) src/*.h
	mkdir -p build-arm
	$(CXX) $(CXXFLAGS) -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard \
	  -static-libstdc++ -static-libgcc -o $@ $(SRCS) -ldl
	arm-linux-gnueabihf-strip $@

clean:
	rm -rf build build-arm

.PHONY: arm clean
