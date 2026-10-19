CXX ?= clang++
CXXFLAGS ?= -O2 -fPIC -std=c++17 -Wall -Wextra -Wpedantic
# Termux-ZRAM has its own Android application id and prefix.
PREFIX ?= /data/data/com.termuxzram/files/usr
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
BINDIR ?= $(PREFIX)/bin
BUILD := build
LZ4_LIB ?= -l:liblz4.so.1
ZSTD_LIB ?= -lzstd
LIBS := $(LZ4_LIB) $(ZSTD_LIB)
# Never export the deliberate corruption hook in the installable library.
TEST_HOOKS ?= 0

all: $(BUILD)/libramz.so $(BUILD)/ramz $(BUILD)/ramz-tests $(BUILD)/ramz-benchmark

$(BUILD):
	mkdir -p $@
$(BUILD)/libramz.so: native/src/ramz.cpp native/include/ramz.h | $(BUILD)
	$(CXX) $(CXXFLAGS) $(if $(filter 1,$(TEST_HOOKS)),-DRAMZ_ENABLE_TEST_HOOKS=1,) -shared -I native/include $< -o $@ $(LIBS)
$(BUILD)/ramz: cli/ramz.cpp $(BUILD)/libramz.so | $(BUILD)
	$(CXX) $(CXXFLAGS) -I native/include $< -L$(BUILD) -lramz -Wl,-rpath,'$$ORIGIN/../lib' -o $@
$(BUILD)/ramz-tests: tests/test_ramz.cpp $(BUILD)/libramz.so | $(BUILD)
	$(CXX) $(CXXFLAGS) $(if $(filter 1,$(TEST_HOOKS)),-DRAMZ_ENABLE_TEST_HOOKS=1,) -I native/include $< -L$(BUILD) -lramz -Wl,-rpath,'$$ORIGIN' -o $@
$(BUILD)/ramz-benchmark: tests/benchmark.cpp $(BUILD)/libramz.so | $(BUILD)
	$(CXX) $(CXXFLAGS) -I native/include $< -L$(BUILD) -lramz -Wl,-rpath,'$$ORIGIN' -o $@

test: $(BUILD)/ramz-tests
	$(BUILD)/ramz-tests
benchmark: $(BUILD)/ramz-benchmark
	$(BUILD)/ramz-benchmark --bytes 33554432 --algorithm lz4
	$(BUILD)/ramz-benchmark --bytes 33554432 --algorithm zstd
install: $(BUILD)/libramz.so $(BUILD)/ramz $(BUILD)/ramz-benchmark
	install -Dm755 $(BUILD)/ramz $(DESTDIR)$(BINDIR)/ramz
	install -Dm755 $(BUILD)/ramz-benchmark $(DESTDIR)$(BINDIR)/ramz-benchmark
	install -Dm755 $(BUILD)/libramz.so $(DESTDIR)$(LIBDIR)/libramz.so
	install -Dm644 native/include/ramz.h $(DESTDIR)$(INCLUDEDIR)/ramz.h
clean:
	rm -rf $(BUILD)
.PHONY: all test benchmark install clean
