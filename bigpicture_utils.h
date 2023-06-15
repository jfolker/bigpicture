#ifndef BP_UTILS_H
#define BP_UTILS_H

#include <stdexcept>
#include <string>

#include <bitshuffle.h>
#include <lz4.h>
#include <simdjson.h>

namespace bigpicture {
  /**
   * The legal values of the "compression" config parameter.
   */
  enum compressor_t : int {
    unknown=-1,
    none=0,
    lz4=1,
    bslz4=2,
  };

  const std::unordered_map<compressor_t, std::string_view>
  compressor_names {
    { compressor_t::unknown, "unknown"},
    { compressor_t::none,    "none" },
    { compressor_t::lz4,     "lz4" },
    { compressor_t::bslz4,   "bslz4" }
  };
  inline std::string_view compressor_name(compressor_t value) {    
    return compressor_names.at(value);
  }

  const std::unordered_map<std::string_view, compressor_t>
  compressor_values {
    { "unknown", compressor_t::unknown},
    { "none",    compressor_t::none},
    { "lz4",     compressor_t::lz4},
    { "bslz4",   compressor_t::bslz4}
  };
  inline compressor_t compressor_value(const std::string_view& name) {    
    return compressor_values.at(name);
  }

  inline std::ostream& operator<<(std::ostream& lhs, compressor_t value) {
    return lhs << compressor_name(value);
  }

  /**
   * Copies the value of a string/number/boolean JSON object field into dest.
   *
   * \throws std::runtime_error if the value is not in src or is not of type T.
   * @precondition dest is a POD type, std::string_view, or std::string.
   */
  template<typename T> inline
  void extract_json_value(T& dest,
			  const simdjson::dom::object& src,
			  const char* name) {
    simdjson::error_code ec;
    src[name].get<T>().tie(dest, ec);
    if (ec) {
      std::stringstream ss;
      ss << "simdjson failed with error code " << ec
	 << "while parsing attribute: \"" << name << "\"." << std::endl;
      throw std::runtime_error(ss.str());
    }
  }
  template<> inline
  void extract_json_value<std::string>(std::string& dest,
				       const simdjson::dom::object& src,
				       const char* name) {
    std::string_view sv;
    extract_json_value(sv, src, name);
    dest = std::string(sv);
  }
  
  /**
   * @return true if a value was successfully extracted from src into dest.
   */
  template<typename T> inline
  bool maybe_extract_json_value(T& dest,
				const simdjson::dom::object& src,
				const char* name) noexcept {
    simdjson::error_code ec;
    src[name].get<T>().tie(dest, ec);
    if (ec) {
      return false;
    }
    return true;
  }
  template<> inline
  bool maybe_extract_json_value<std::string>(std::string& dest,
					     const simdjson::dom::object& src,
					     const char* name) noexcept {
    std::string_view sv;
    if (!maybe_extract_json_value(sv, src, name)) {
      return false;
    }
    dest = std::string(sv);
    return true;
  }

  /**
   * Copies the value of a string/number/boolean JSON object field into dest 
   * using JSON pointer syntax.
   *
   * \throws std::runtime_error if the value is not in src or is not of type T.
   * @precondition dest is a POD type, std::string_view, or std::string
   */
  template<typename T> inline
  void extract_json_pointer(T& dest,
			    const simdjson::dom::object& src,
			    const char* jsp) {
    simdjson::simdjson_result<T> tmp;
    tmp = src.at_pointer(jsp).get<T>();
    if (tmp.error()) {
      std::stringstream ss;
      ss << "simdjson error while retrieving JSON pointer value \""
	 << jsp << "\":" << tmp.error() << std::endl;
      throw std::runtime_error(ss.str());
    }
    dest = static_cast<T>(tmp.value());
  }
  template<> inline
  void extract_json_pointer<std::string>(std::string& dest,
					 const simdjson::dom::object& src,
					 const char* jsp) {
    std::string_view sv;
    extract_json_pointer(sv, src, jsp);
    dest = std::string(sv);
  }

  /**
   * @return true If the value was successfully extracted from src into dest.
   */
  template<typename T> inline
  bool maybe_extract_json_pointer(T& dest,
				  const simdjson::dom::object& src,
				  const char* jsp) noexcept {
    simdjson::simdjson_result<T> tmp;
    tmp = src.at_pointer(jsp).get<T>();
    if (tmp.error()) {
      return false;
    }
    dest = static_cast<T>(tmp.value());
    return true;
  }
  template<> inline
  bool maybe_extract_json_pointer<std::string>(std::string& dest,
					       const simdjson::dom::object& src,
					       const char* jsp) noexcept {
    std::string_view sv;
    if (!maybe_extract_json_pointer<std::string_view>(sv, src, jsp)) {
      return false;
    }
    dest = std::string(sv);
    return true;
  }

  /**
   * Loads the json-based config file into memory and returns a simdjson-based 
   * deserialized representation.
   *
   * @param filename The name of the file, similar to the filename argument 
   *                 to fopen().
   * @return A reference to the top-level object of the config file.
   *
   * \throws std::runtime_error if the file cannot be read, is ill-formed, 
   *         or contains invalid parameters.
   *
   * @todo Add config parameter validation.
   */
  const simdjson::dom::object& load_config_file(const std::string& filename);

  /**
   * A convenience utility wrapper around std::unique_ptr<char[]>.
   *
   * 1. Pointer conversion operators return the underlying raw buffer.
   * 2. Numeric conversion operators return the buffer size.
   * 3. Comparison operators compare against the buffer size.
   * 4. The boolean conversion operator returns true if the buffer is nonzero size.
   *
   * @note We use our own managed buffer class instead of std::vector<char> because 
   *       std::vector incurs significant additional cost to support functionality 
   *       beyond our scope.
   *
   * TODO: Add a capacity parameter and treat the reserved size and used data separately.
   */
  class unique_buffer {
  public:
    constexpr unique_buffer() noexcept : m_len(0) {}
    unique_buffer(size_t uncompressed_size) :
      m_len(uncompressed_size), m_data(new char[uncompressed_size]) {}

    constexpr char*  get()  const { return m_data.get(); }
    constexpr size_t size() const { return m_len; }
    
    void reset(size_t uncompressed_size=0) {
#ifndef NDEBUG
      // In debug builds, expose attempts to read old data after a reset.
      memset(m_data.get(), 'x', m_len);
#endif
      if (uncompressed_size == m_len) {
	return;
	
      } else if (uncompressed_size == 0) {
	m_len = 0;
	m_data = nullptr;
	
      } else {
	m_len = uncompressed_size;
	m_data = std::unique_ptr<char[]>(new char[m_len]);
      }
    }

    /**
     * @param element_size The size of each "word" of data, e.g. the number of 
     *                     bytes per pixel for an image.
     * @precondition The buffer size is equal to the decoded size of the data.
     * @warning Vulnerable to buffer overflow attacks if precondition is violated.
     *          See bslz4_decode() below.     
     */
    void decode(compressor_t codec, const void* src, size_t src_len,
		size_t element_size=4) {
      switch (codec) {
      case compressor_t::bslz4:
	bslz4_decode(src, src_len, element_size);
	break;
      case compressor_t::lz4:
	lz4_decode(src, src_len);
	break;
      case compressor_t::none:
	memcpy(m_data.get(), src, src_len);
	break;
      default:
	std::stringstream ss;
	ss << "Error in unique_buffer::decode() : codec " << codec
	   << " unsupported" << std::endl;
	throw std::runtime_error(ss.str());
      }
    }

    /**
     * @return The compressed size of the data, which is less than or equal 
     *         to size().
     */
    int64_t encode(compressor_t codec, const void* src, size_t src_len,
		   size_t element_size=4) {
      switch (codec) {
      case compressor_t::bslz4:
	return bslz4_encode(src, src_len, element_size);
      case compressor_t::lz4:
	return lz4_encode(src, src_len);
      case compressor_t::none:
	memcpy(m_data.get(), src, src_len);
	return src_len;
      default:
	std::stringstream ss;
	ss << "Error in unique_buffer::encode() : codec " << codec
	   << " unsupported" << std::endl;
	throw std::runtime_error(ss.str());
      }
      return -1;
    }

    /**
     * @param cbuf The buffer containing the compressed data to be decoded.
     * @param compressed_size The size of cbuf in bytes.
     * @param element_size The size of each "word" of data, e.g. the number of 
     *                     bytes per pixel for an image.
     * @precondition The buffer size is equal to the decoded size of the data.
     * @warning Vulnerable to buffer overflow attacks if precondition is violated.
     * @todo Fork bitshuffle and implement safe versions of bshuf_decompress_*.
     */
    void bslz4_decode(const void* cbuf, size_t compressed_size,
		      size_t element_size=4) {
      size_t n_elements = m_len/element_size;
      int64_t decomp_result = bshuf_decompress_lz4(cbuf, m_data.get(),
						   n_elements, element_size, 0);
      if (decomp_result < 0) {
	std::stringstream ss;
	ss << "bshuf_decompress_lz4() failed with status "
	   << decomp_result << std::endl;
	throw std::runtime_error(ss.str());
      } else if (static_cast<size_t>(decomp_result) != compressed_size) {
	std::stringstream ss;
	ss << "bshuf_decompress_lz4() failed to decompress all data. Processed "
	   << decomp_result << " out of " << compressed_size << " bytes." << std::endl;
	throw std::runtime_error(ss.str());
      }
    }
    
    int64_t bslz4_encode(const void* src, size_t uncompressed_size,
			 size_t element_size=4) {
      size_t n_elements = uncompressed_size/element_size;
      size_t upper_bound = bshuf_compress_lz4_bound(n_elements, element_size, 0);
      if (upper_bound == 0) {
	throw std::runtime_error("bshuf_compress_lz4_bound() detected bad data.");
      } else if (m_len < upper_bound) {
	reset(upper_bound);
      }      
      int64_t comp_result = bshuf_compress_lz4(src, m_data.get(),
					       n_elements, element_size, 0);
      if (comp_result < 0) {
	std::stringstream ss;
	ss << "bshuf_compress_lz4() failed with status "
	   << comp_result << std::endl;
	throw std::runtime_error(ss.str());
      }
      return comp_result;
    }

    /**
     * @param cbuf The buffer containing the compressed data to be decoded.
     * @param compressed_size The size of cbuf in bytes.
     * @precondition The buffer size is equal to the decoded size of the data.
     */
    void lz4_decode(const void* cbuf, size_t compressed_size) {
      int64_t decomp_result = LZ4_decompress_safe(static_cast<const char*>(cbuf),
						  m_data.get(),
						  compressed_size, m_len);
      if (decomp_result < 0) {
	std::stringstream ss;
	ss << "LZ4_decompress_safe() failed with status "
	   << decomp_result << std::endl;
	throw std::runtime_error(ss.str());
	
      } else if (static_cast<size_t>(decomp_result) != m_len) {
	std::stringstream ss;
	ss << "LZ4_decompress_safe() decompressed "
	   << decomp_result  << " bytes, expected "
	   << m_len << " bytes." << std::endl;
	throw std::runtime_error(ss.str());
      }
    }

    int64_t lz4_encode(const void* src, size_t uncompressed_size) {
      size_t upper_bound = static_cast<size_t>(LZ4_compressBound(uncompressed_size));
      if (upper_bound == 0) {
	throw std::runtime_error("LZ4_compressBound() detected bad data.");
      } else if (m_len < upper_bound) {
	reset(upper_bound);
      }
      int64_t comp_result = LZ4_compress_default(static_cast<const char*>(src),
						 m_data.get(),
						 uncompressed_size, m_len);
      if (comp_result < 0) {
	std::stringstream ss;
	ss << "LZ4_compress_default() failed with status "
	   << comp_result << std::endl;
	throw std::runtime_error(ss.str());
      }
      return comp_result;
    }
    
  private:
    size_t m_len;
    std::unique_ptr<char[]> m_data;
  };
}

#endif
