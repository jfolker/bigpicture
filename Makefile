# This file was designed for gmake (not pmake).
#
# All dependencies excpet libc, libc++, and cryptographic libraries should
# be statically linked. This is to simplify installation for the user and
# minimize unexpected runtime behavior.
#
# Note: additional header-only dependencies:
#   boost 1.75.0, https://www.boost.org/
#   cppzmq 4.9.0, https://github.com/zeromq/cppzmq   
#
#
CC := clang
CXX := clang++
LD := lld

HEADERS := bigpicture_utils.h dectris_utils.h dectris_stream.h stream_to_cbf.h
OBJECTS := bigpicture_utils.o dectris_utils.o stream_to_cbf.o
STATIC_LIB := libbigpicture.a
SHARED_LIB := libbigpicture.so
EXECUTABLES := bparchived bpcompressd bpindexd

UNIT_TESTS := test_dectris_stream
INTEGRATION_TESTS := test_bparchived

LIB_DIRS := -L ./ -L ./deps/usr/local/lib \
	-L /usr/lib/x86_64-linux-gnu/hdf5/serial \
	-L /usr/lib/x86_64-linux-gnu \
	-L /usr/lib -L /usr/local/lib

# Anything that doesn't meet the criteria for being dynamically-linked described below
# should be statically-linked.
STATIC_DEPS := -l:libbsd.a -l:libsodium.a -l:libpgm.a -l:libnorm.a -l:libprotokit.a \
	-l:libzmq.a -l:libsimdjson.a -l:libhdf5.a -l:libbitshuffle.a -l:libcbf.a \
	-l:libturbojpeg.a

# All system libraries (shipped with the OS) and crypto libraries should
# be dynamically-linked.
# TODO: libbsd (a compatibility shim for linux) shouldn't be linked on any BSD system.
DYNAMIC_DEPS := -lpthread -lunwind -lbsd -lcrypto -lgnutls -lgssapi_krb5 \
	-lsodium -lpgm -lnorm -lprotokit

DEPS = $(LIB_DIRS) $(STATIC_DEPS) $(DYNAMIC_DEPS)


# TODO: Use -stdlib=libc++, requires building libzmq with libc++ as well.
#       This is currently not feasible because 'apt install libc++-dev'
#       clobbers the version of libunwind we rely on.
# NOTE: Unused parameters when writing a callback to be used by another
#       library are reasonable, so we'll tolerate them.
CXX_COMMON_FLAGS =-std=c++17 -fopenmp=libomp -Wall -Wextra -Wno-unused-parameter -Werror \
  -I ./deps/usr/local/include \
  -I /usr/include/hdf5/serial \
  -I /usr/local/include/bsd -I /usr/include/bsd \
  -I /usr/local/include -I /usr/include \
  -DZMQ_BUILD_DRAFT_API=1 -DLIBBSD_OVERLAY

ASAN_FLAGS = -fsanitize=address -fsanitize-address-use-after-return=always \
  -fsanitize-address-use-after-scope -fsanitize-address-outline-instrumentation \
  -fsanitize-address-poison-custom-array-cookie -fsanitize-address-use-odr-indicator
COVERAGE_FLAGS = -fprofile-instr-generate -fcoverage-mapping -mllvm -runtime-counter-relocation
CXX_DEBUG_FLAGS = -O0 -glldb -fno-omit-frame-pointer $(COVERAGE_FLAGS) $(ASAN_FLAGS)

CXX_RELEASE_FLAGS = -O3 -glldb -DNDEBUG -DBOOST_TEST_DYN_LINK

ifeq ($(DEBUG),)
CXXFLAGS = $(CXX_COMMON_FLAGS) $(CXX_RELEASE_FLAGS)
else
CXXFLAGS ?= $(CXX_COMMON_FLAGS) $(CXX_DEBUG_FLAGS)
endif

#LDFLAGS = $(DEPS)

.PHONY: default
default: build

# TODO: Files in bin/, lib/, and include/ are not cluttering the src directory,
#       but it would still be nice to remove them.
.PHONY: clean
clean:
	rm -f $(UNIT_TESTS) $(OBJECTS) $(STATIC_LIB) $(EXECUTABLES) \
		*.log *.out *.err *.dump *.cbf *.profraw

.PHONY: install
install: build
	install --mode=0755 bparchived      /usr/local/bin
	install --mode=0755 bpindexd        /usr/local/bin
	install --mode=0755 bpcompressd     /usr/local/bin
	install --mode=0755 bigpicture      /usr/local/bin
	install --mode=0644 libbigpicture.a /usr/local/lib
	install --mode=0644 *.h             /usr/local/include/bigpicture
	install --mode=0644 config.json     /etc/bigpicture

.PHONY: build
build: Makefile deps include lib bin

.PHONY: docs
docs:
	@doxygen ./Doxyfile


#
# Include targets
#
%.h:
	mkdir -p include/
	cp $@ include/
include: $(HEADERS)

#
# Library targets
#
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link our own symbols, and leave out dependencies.
%.a: $(OBJECTS) $(HEADERS)
	ar -crs $@ $(OBJECTS)
.PHONY: static-lib
static-lib: $(STATIC_LIB)

.PHONY: lib
lib: static-lib


#
# Executable targets
#
bp%d: bp%d.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -fuse-ld=$(LD) -o $@ $< $(DEPS) -l:$(STATIC_LIB)

bigpicture: bigpicture.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -fuse-ld=$(LD) -o bigpicture bigpicture.cpp $(DEPS) -l:$(STATIC_LIB)

bin: $(EXECUTABLES)

#
# Testing targets
# TODO: Move tests to a separate directory with its own separate Makefile.
# TODO: Write some integration tests!
#
TEST_BUILD_FLAGS := -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK -DBOOST_TEST_MAIN
.PHONY: test
test: unit_tests

test_%: test_%.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -fuse-ld=$(LD) -o ./$@ $< $(DEPS) $(TEST_BUILD_FLAGS) -l:$(STATIC_LIB)

.PHONY: unit_tests
unit_tests: $(UNIT_TESTS)
	$(foreach utest,$(UNIT_TESTS),./$(utest) || echo)

.PHONY: integration_tests
integration_tests:
	$(foreach itest,$(INTEGRATION_TESTS),./$(itest) || echo)

#
# Build all bigpicture dependencies, only performed on first build.
# Note: 'make clean' does not uninstall dependencies.
#
.PHONY: deps
deps: deps.installed
deps.installed:
	./install-deps.sh

.PHONY: clean-deps
clean-deps:
	rm -rf ./deps/bin ./deps/lib ./deps/include ./deps/bitshuffle/build/*
