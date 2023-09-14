Important:
  Until the first release (1.0.0), every part of the bigpicture API and every config file parameter is subject 
  to change.

  For now, this README.txt is a temporary stand-in explaining the how to use the very rudimentary build 
  infrastructure, run tests, and use the current working functionality.

  bparchived currently works, albeit only for Dectris detectors using the SIMPLON "Stream" v1 interface, and 
  the only supported output type is minicbf (1 CBF file per image).

  bigpicture, bpcompressd, and bpindexd are not yet implemented.
    "bpcompressd" will generate images viewable in a web browser (e.g. JPEGs) using raw images produced by
    bparchived.
    
    "bpindexd" will index raw images produced by bparchived.
    
    "bigpicture" will monitor and coordinate bparchived, bpindexd, bpcompressd, and any other services 
    associated with bigpicture.

Instructions:
  First-time setup:
    Before using or contributing to bigpicture, some dependencies must be installed via your operating 
    system's package manager. Instructions are provided below for Ubuntu 22.04, but other Linux distributions 
    and FreeBSD are supported by obtaining equivalent packages.
    
    After installing the required dependencies and cloning the repository as shown below (using the url for 
    this repository in 'git clone'), building for the first time and all subsequent times is as simple as 
    running 'make' with no arguments. The default Makefile builds all submodule dependencies if it detects they
    have not yet been built, and installs all submodule dependencies to the 'deps/usr/local/' subdirectory of 
    this repository. Consequently, rebuilding without building dependencies is also done via 'make'.
    
    After building bigpicture, it is recommended to run all of the tests ('make test') to confirm all 
    functionality works as intended before making changes or deploying into production.
    
    Obtaining dependencies (Ubuntu):
      sudo apt install pkg-config python3-pip clang llvm libgomp1 libunwind-dev libboost-all-dev \
      openssl libbsd-dev libcbf-dev libgnutls30 libgssapi-krb5-2 libhdf5-dev libnorm-dev libpgm-dev \
      libsodium-dev libturbojpeg-dev
    
    Cloning the repository with all its submodule dependencies:
      git clone <repository url>
      cd bigpicture
      git submodule update --init
    
    Building (first time and all subsequent times):
      make
      
    Running tests (first time and all subsequent times):
      make test
      
    Note: We do not use install submodules recursively because some extra submodules within the dependencies 
    install headers which replace system headers, e.g. an 'errno.h' file that does not contain the necessary 
    macro definitions. The submodules we use minus their submodules are sufficient to build all of the 
    functionality of bigpicture.
    
bparchived [-c config_file] :
  Connects to a Dectris DCU via the "Stream" interface using a ZeroMQ pull socket and writes each 
  image to its own CBF file, a format informally known among crystallographers as "minicbf".
  
  If no config file is specified, the default config file is loaded from "/etc/bigpicture/config.json".
  
  Note that the CBF file format does not support LZ4 or bitshuffle-LZ4 compression, hence all images must be 
  decompressed before committing to disk. This format is severely limited in practical use, but was chosen 
  because utilities currently used by LS-CAT to index images, such as CCP4 and BEST require it. This may 
  change in the future, and hopefully it will.
