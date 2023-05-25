Important:
  Until the first release (1.0.0), every part of the bigpicture API and every config file parameter 
  is subject to change.

  For now, this README.txt is a temporary stand-in explaining the how to use the very rudimentary 
  build infrastructure, run tests, and use the current working functionality.

  bparchived currently works, albeit only for Dectris detectors using the SIMPLON "Stream" interface, 
  and the only supported output type is minicbf (1 CBF file per image).

  bigpicture, bpcompressd, and bpindexd are not yet implemented.
    "bpcompressd" will generate images viewable in a web browser (e.g. JPEGs) using raw images produced 
    by bparchived.
    
    "bpindexd" will index raw images produced by bparchived.
    
    "bigpicture" will monitor and coordinate bparchived, bpindexd, bpcompressd, and any other services 
    associated with bigpicture.

Instructions:
  Obtaining dependencies (Ubuntu):
    sudo apt install clang llvm boost openssl libbsd-dev libcbf-dev libgnutls30 libgomp1 \
    libgssapi-krb5-2 libhdf5-dev libnorm1 libpgm-5.3.0 libsodium23 libturbojpeg-dev \
    libzmq3-dev libunwind-14
    
  Building for the First Time:
    make bitshuffle # only needs to be ran
    make 
    make test
  
  Subsequent Builds:
    make

  Testing:
    make test
    
bparchived [-c config_file] :
  Connects to a Dectris DCU via the "Stream" interface using a ZeroMQ pull socket and writes each 
  image to its own CBF file, a format informally known among crystallographers as "minicbf".
  
  If no config file is specified, the default config file is loaded from "/etc/bigpicture/config.json".
  
  Note that the CBF file format does not support compression, hence all images must be decompressed 
  before committing to disk. This format is severely limited in practical use, but was chosen because 
  utilities currently used by LS-CAT to index images, such as CCP4 and BEST require it. This may 
  change in the future, but hopefully it will.
