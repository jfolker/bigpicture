#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <simdjson.h>

#include "bigpicture_utils.h"

using namespace bigpicture;

static const std::unordered_map<compressor_t, std::string>
s_compress_names {
  { compressor_t::unknown, "unknown"},
  { compressor_t::none,    "none" },
  { compressor_t::lz4,     "lz4" },
  { compressor_t::bslz4,   "bslz4" }
};
/*
  This cannot be inlined because of the following bogus compiler error:
  
  ld.lld: error: undefined symbol: bigpicture::compressor_name[abi:cxx11](bigpicture::compressor_t)
  >>> referenced by dectris_utils.cpp
  >>>               dectris_utils.o:(bigpicture::detector_config_t::to_json[abi:cxx11]())
  clang: error: linker command failed with exit code 1 (use -v to see invocation)  
*/
/*inline*/ const std::string&
bigpicture::compressor_name(compressor_t x) {
  return s_compress_names.at(x);
}

static simdjson::dom::element  s_empty_element;
static simdjson::dom::object   s_empty_object;

static bool s_config_loaded = false;
static std::string             s_config_file_name;
static simdjson::dom::parser   s_config_parser;
static simdjson::dom::element& s_config_root = s_empty_element;
static simdjson::dom::object&  s_config_object = s_empty_object;
const simdjson::dom::object&
bigpicture::load_config_file(const std::string& filename) {
  using namespace simdjson::dom;
  
  /*
    TODO: For interns, new hires, or anyone with free time, add validation 
    for config parameters. 

    The sky is the limit for improving usability. Instead of writing to stderr 
    and exiting, remember to throw an exception which contains a documentation-quality 
    error message. A good error message has the following traits at a minimum:
    
    1. It is free from spelling/grammatical errors and uses complete sentences.
    
    2. It shows due respect to the user; this means respecting the reader's time 
    and intelligence.
    
    3. It plainly, but politely explains the cause of the error in layman's terms 
    as best as it is known.
    
    4. Gives the best possible instructions for remediation:
      (a) If there is a clear-cut solution, it provides simple directions.
      (b) If the solution is long-winded, it refers the user to the appropriate 
          documentation.
      (c) If there is no known solution, it refers the user to the appropriate 
          point of contact.
    
    This is not exciting work, but it is essential and your contribution is
    appreciated.
  */ 
  if (filename.compare(s_config_file_name) == 0) {
#ifndef NDEBUG
    throw std::runtime_error("load_config_file() called twice");
#endif
    return s_config_object; // config file already loaded
  }
  
  if (filename.empty() || !std::filesystem::exists(filename)) {
    std::stringstream ss;
    ss << "ERROR: Config file " << filename << " does not exist." << std::endl;
    throw std::runtime_error(ss.str());
  }

  // Parameter validation goes here.
  
  s_config_root = s_config_parser.load(filename);
  if (!s_config_root.is<object>()) {
    s_config_loaded = false;
    throw std::runtime_error("The root of the JSON config file hierarchy should be an object, not an array");
  }
  s_config_object = s_config_root.get_object();
  s_config_loaded = true;
  
  return s_config_object;
}
