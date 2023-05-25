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
LIB_DIRS := -L /opt/zmq-draft/usr/lib/x86_64-linux-gnu -L /usr/lib/x86_64-linux-gnu \
		-L /usr/lib/x86_64-linux-gnu/hdf5/serial -L /usr/local/lib -L ./lib
ZMQ_STATIC_DEPS := -l:libbsd.a -l:libsodium.a -l:libpgm.a -l:libnorm.a -l:libprotokit.a -l:libzmq.a
ZMQ_DYNAMIC_DEPS := -lbsd -lsodium -lpgm -lnorm -lprotokit -lzmq

DEPS :=  $(LIB_DIRS) \
	-lpthread -lgomp -lunwind -lcrypto -lgnutls -lgssapi_krb5 \
	$(ZMQ_DYNAMIC_DEPS) \
	-l:libhdf5.a -l:bitshuffle.a -l:libcbf.a -l:libturbojpeg.a \
	-l:libjxl.a -l:libsimdjson.a

HEADERS := bigpicture_utils.h dectris_utils.h dectris_stream.h stream_to_cbf.h
OBJECTS := bigpicture_utils.o dectris_utils.o stream_to_cbf.o

# TODO: This HDF5 include path is Ubuntu-specific, find a way
# to find HDF5 on RHEL and FreeBSD.
CXX_COMMON_FLAGS =-std=c++17 -Wall -Werror -I /opt/zmq-draft/usr/include/ \
  -I /usr/include/hdf5/serial -I /usr/local/include/bsd -I ./bitshuffle/src \
  -DZMQ_BUILD_DRAFT_API=1 -DLIBBSD_OVERLAY

ASAN_FLAGS = -fsanitize=address -fsanitize-address-use-after-return=always \
  -fsanitize-address-use-after-scope -fsanitize-address-outline-instrumentation \
  -fsanitize-address-poison-custom-array-cookie -fsanitize-address-use-odr-indicator
COVERAGE_FLAGS = -fprofile-instr-generate -fcoverage-mapping #-mllvm -runtime-counter-relocation
CXX_DEBUG_FLAGS = -O0 -glldb -fno-omit-frame-pointer $(COVERAGE_FLAGS) $(ASAN_FLAGS)

CXX_RELEASE_FLAGS = -O2 -glldb -DNDEBUG

CC := clang
CXX := clang++

ifeq ($(DEBUG),)
CXXFLAGS = $(CXX_COMMON_FLAGS) $(CXX_RELEASE_FLAGS)
else
CXXFLAGS ?= $(CXX_COMMON_FLAGS) $(CXX_DEBUG_FLAGS)
endif

LDFLAGS =  $(DEPS) $(LIB_DIRS)

.PHONY: default
default: build

.PHONY: clean
clean:
	rm -f bigpicture bparchived bpindexd bpcompressd test_integration *.o *.a *.so *.log *.out *.err *.dump *.cbf

.PHONY: install
install:
	install --mode=0755 bigpicture /usr/local/bin
	install --mode=0755 bparchived /usr/local/bin
	install --mode=0755 bpindexd /usr/local/bin
	install --mode=0755 bpcompressd /usr/local/bin
	install --mode=0644 config.json /etc/bigpicture

.PHONY: build
build: Makefile $(OBJECTS) bparchived bpindexd bpcompressd bigpicture

.PHONY: docs
docs:
	@doxygen ./Doxyfile

%.h:
	mkdir -p include/
	cp $@ include/

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build our dependencies for lz4 and for bslz4 decompression.
# The LZ4 implementation is compiled into the same static lib as bslz4.
# This only needs to be done when the bitshuffle submodule is updated
.PHONY: bitshuffle
bitshuffle:
	mkdir -p include/ lib/ && rm -f lib/bitshuffle.a
	cd include && cp ../bitshuffle/lz4/*.h ./ && cp ../bitshuffle/src/*.h ./ && cd -
	cd bitshuffle && python3 setup.py build_ext && cd -
	ar -crs lib/bitshuffle.a $(shell find bitshuffle/build -name '*.o')

bigpicture: bigpicture.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o bigpicture bigpicture.cpp $(OBJECTS) $(DEPS)

bparchived: bparchived.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o bparchived bparchived.cpp $(OBJECTS) $(DEPS)

bpindexd: bpindexd.cpp *.h $(OBJECT) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o bpindexd bpindexd.cpp $(OBJECTS) $(DEPS)

bpcompressd: bpcompressd.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o bpcompressd bpcompressd.cpp $(OBJECTS) $(DEPS)

test_integration: test_integration.cpp $(OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o test_integration test_integration.cpp $(OBJECTS) $(DEPS) -l:libboost_unit_test_framework.a

.PHONY: test
test: test_integration
	./test_integration

