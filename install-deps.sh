#!/bin/bash
set -ue

BP_BASEDIR=$PWD
cd deps

# All deps were originally built and tested on GCC, so we must use it.
# TODO: Fix build problems with clang on some deps and submit a pull request.
export CMAKE_BUILD_PARALLEL_LEVEL=4
export CC=gcc
export CXX=g++
export CMAKE_C_COMPILER=$CC
export CMAKE_CXX_COMPILER=$CXX

export DESTDIR=$PWD
export CMAKE_INSTALL_PREFIX=$PWD
export CMAKE_LIBRARY_PATH=$CMAKE_INSTALL_PREFIX/usr/local/lib
rm -rf usr/ && mkdir -p usr/
rm -rf build/ && mkdir -p build/
cd build


# Install libzmq
#
# Because we are building out of a git repo, libzmq will build the
# draft API we need for zmq_poller.
rm -rf libzmq && mkdir -p libzmq && cd libzmq
cmake -DENABLE_DRAFTS=ON ../../libzmq
make -j$CMAKE_BUILD_PARALLEL_LEVEL && make install
#make test
cd ..


# Install cppzmq
rm -rf cppzmq && mkdir -p cppzmq && cd cppzmq
cmake -DENABLE_DRAFTS=ON ../../cppzmq
make -j$CMAKE_BUILD_PARALLEL_LEVEL && make install
make test
cd ..

# Install simdjson
rm -rf simdjson && mkdir -p simdjson && cd simdjson
cmake -DSIMDJSON_DEVELOPER_MODE=ON ../../simdjson
cmake --build . --config Release
ctest
make -j$CMAKE_BUILD_PARALLEL_LEVEL && make install
cd ..

# Install bitshuffle
# Unlike cmake-based builds, we build inside the repo dir.
#
# TODO: Run bitshuffle's tests. It doesn't have a target to run tests,
# just a handful of python scripts. I am considering forking to build
# a static library of bitshuffle without lz4 symbols in order to use
# the OS's lz4 under /usr or /usr/local.
cd ../bitshuffle
python3 setup.py build_ext
for header in $(find . -name '*.h'); do
    cp $header ../usr/local/include/$(basename $header)
done
ar -crs ../usr/local/lib/libbitshuffle.a $(find . -name '*.o')

cd ../../
touch deps.installed
