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

# TODO: Use lld once 
CC := clang
CXX := clang++
LD := ld

HEADERS := bigpicture_utils.h dectris_utils.h dectris_stream.h stream_to_cbf.h
OBJECTS := bigpicture_utils.o dectris_utils.o stream_to_cbf.o
STATIC_LIBS := libbigpicture.a
SHARED_LIBS := libbigpicture.so
EXECUTABLES := bparchived bpcompressd bpindexd

UNIT_TESTS := test_dectris_stream
INTEGRATION_TESTS := test_bparchived

LIB_DIRS := -L /opt/zmq-draft/usr/lib/x86_64-linux-gnu -L /usr/lib/x86_64-linux-gnu \
		-L /usr/lib/x86_64-linux-gnu/hdf5/serial -L /usr/local/lib -L ./lib
#ZMQ_STATIC_DEPS := -l:libbsd.a -l:libsodium.a -l:libpgm.a -l:libnorm.a -l:libprotokit.a -l:libzmq.a
ZMQ_DYNAMIC_DEPS := -lbsd -lsodium -lpgm -lnorm -lprotokit -lzmq

DEPS :=  $(LIB_DIRS) \
	-lpthread -lgomp -lunwind -lcrypto -lgnutls -lgssapi_krb5 \
	$(ZMQ_DYNAMIC_DEPS) \
	-l:libhdf5.a -l:bitshuffle.a -l:libcbf.a -l:libturbojpeg.a \
	-l:libsimdjson.a #-l:libjxl.a



# TODO: This HDF5 include path is Ubuntu-specific, find a way
#       to find HDF5 on RHEL and FreeBSD.
# TODO: Use -stdlib=libc++, requires building libzmq with libc++ as well.
#       This is currently not feasible because 'apt install libc++-dev'
#       uninstalls the version of libunwind we rely on.
CXX_COMMON_FLAGS =-std=c++17 -Wall -Wextra -Werror -I /opt/zmq-draft/usr/include/ \
  -I /usr/include/hdf5/serial -I /usr/local/include/bsd -I ./bitshuffle/src \
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

LDFLAGS = $(DEPS)

.PHONY: default
default: build

# TODO: Files in bin/, lib/, and include/ are not cluttering the src directory,
#       but it would still be nice to remove them.
.PHONY: clean
clean:
	rm -f $(UNIT_TESTS) $(OBJECTS) *.log *.out *.err *.dump *.cbf *.profraw

.PHONY: install
install: build
	install --mode=0755 -t /usr/local/bin ./bin/*
	install --mode=0755 -t /usr/local/lib ./lib/*
	install --mode=0755 -t /usr/local/lib ./lib/*
	install --mode=0755 bparchived /usr/local/bin
	install --mode=0755 bpindexd /usr/local/bin
	install --mode=0755 bpcompressd /usr/local/bin
	install --mode=0644 config.json /etc/bigpicture

.PHONY: build
build: Makefile include lib bin

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
# Lib targets
#
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.a: $(OBJECTS) $(HEADERS)
	mkdir -p ./lib/
	ar -crs ./lib/$@ $(OBJECTS)
.PHONY: static-libs
static-libs: $(STATIC_LIBS)

# TODO: Building a shared library is broken for now, and -fPIC is not set,
#       which is also fraught with troubles. For now
%.so: $(OBJECTS) $(HEADERS)
	mkdir -p ./lib/
	$(LD) -o ./lib/$@ $(OBJECTS) $(DEPS)
.PHONY: shared-libs
shared-libs: $(SHARED_LIBS)

lib: static-libs


#
# Bin targets
# TODO: Should we link against the static lib instead of each individual object?
#       It would make passing test results more reassuring.
#
bp%d: bp%d.cpp $(OBJECTS) $(HEADERS)
	mkdir -p ./bin/
	$(CXX) $(CXXFLAGS) -o ./bin/$@ $< $(OBJECTS) $(DEPS)

bigpicture: bigpicture.cpp $(OBJECTS) $(HEADERS)
	mkdir -p ./bin/
	$(CXX) $(CXXFLAGS) -o ./bin/bigpicture bigpicture.cpp $(OBJECTS) $(DEPS)

bin: $(EXECUTABLES)

#
# Testing targets
# TODO: Move tests to a separate directory with its own separate Makefile.
# TODO: Write some integration tests!
#
TEST_BUILD_FLAGS := -lboost_unit_test_framework -DBOOST_TEST_DYN_LINK -DBOOST_TEST_MAIN
.PHONY: test
test: unit_tests

test_%: test_%.cpp
	$(CXX) $(CXXFLAGS) -o ./$@ $< $(OBJECTS) $(DEPS) $(TEST_BUILD_FLAGS)

.PHONY: unit_tests
unit_tests: $(UNIT_TESTS)
	$(foreach utest,$(UNIT_TESTS),./$(utest) || echo)

.PHONY: integration_tests
integration_tests:
	$(foreach itest,$(INTEGRATION_TESTS),./$(itest) || echo)

#
# Dependency targets
# TODO: Build cppzmq, simdjson, and our own special version of libzmq that enables
#       the poller library.
#
#.PHONY: deps
#deps: install-deps.sh

# Dependencies for lz4 and for bslz4 decompression.
# The LZ4 implementation is compiled into the same static lib as bslz4.
# This only needs to be done when the bitshuffle submodule is updated.
# TODO: Fork bitshuffle and have it build its own static and shared libs,
#       putting no deps into the static lib, and dynamically linking
#       everything necessary for the shared lib.
.PHONY: deps
bitshuffle:
	mkdir -p include/ lib/ && rm -f lib/bitshuffle.a
	cd include && cp ../bitshuffle/lz4/*.h ./ && cp ../bitshuffle/src/*.h ./ && cd -
	cd bitshuffle && python3 setup.py build_ext && cd -
	ar -crs lib/bitshuffle.a $(shell find bitshuffle/build -name '*.o')

