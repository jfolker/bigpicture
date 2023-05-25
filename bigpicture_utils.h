#ifndef BP_UTILS_H
#define BP_UTILS_H

#include <stdexcept>
#include <string>

#include <simdjson.h>

namespace bigpicture {
  /**
   * Validates the "htype" field of a global header message part or an image
   * header message part. If the htype is incorrect, an exception is thrown.
   *
   * \throws std::runtime_error if htype field does not match expected value.
   *
   * @note Intended for use as a helper function for user-implemented stream_parser 
   *       subclasses and used internally by dectris_global_data.
   */
  inline void validate_htype(const simdjson::dom::object& record,
			     const char expected_htype[]) {
    simdjson::error_code ec;
    std::string_view htype_actual;
    record["htype"].get<std::string_view>().tie(htype_actual, ec);
    if (ec || htype_actual.compare(expected_htype) != 0) {
      std::stringstream ss;
      ss << "Expected htype: " << expected_htype << ", actual: " << htype_actual << std::endl;
      throw std::runtime_error(ss.str());
    }
  }

  /**
   * Copies the value of a string/number/boolean JSON object field into dest.
   *
   * \throws std::runtime_error if the value is not in src or is not of type T.
   */
  template<typename T> inline
  void extract_simdjson_number(T& dest,
			       const simdjson::dom::object& src,
			       const char* name) {
    simdjson::error_code ec;
    src[name].get<T>().tie(dest, ec);
    if (ec) {
      std::stringstream ss;
      ss << "The DCU did not provide a valid config value for \"" << name << "\"" << std::endl;
      throw std::runtime_error(ss.str());
    }
  }

  /**
   * @return true if a value was successfully extracted from the json to the destination
   */
  template<typename T> inline
  bool maybe_extract_simdjson_number(T& dest,
				     const simdjson::dom::object& src,
				     const char* name) {
    simdjson::error_code ec;
    src[name].get<T>().tie(dest, ec);
    if (ec) {
      return false;
    }
    return true;
  }
  
  inline void extract_simdjson_string(std::string& dest,
				      const simdjson::dom::object& src,
				      const char* name) {
    simdjson::error_code ec;
    std::string_view sv;
    src[name].get<std::string_view>().tie(sv, ec);
    if (ec) {
      std::stringstream ss;
      ss << "The DCU did not provide a valid config value for \"" << name << "\"" << std::endl;
      throw std::runtime_error(ss.str());
    }
    dest = std::string(sv);
  }

  /**
   * @return true if a value was successfully extracted from the json to the destination
   */
  inline bool maybe_extract_simdjson_string(std::string& dest,
					    const simdjson::dom::object& src,
					    const char* name) {
    simdjson::error_code ec;
    std::string_view sv;
    src[name].get<std::string_view>().tie(sv, ec);
    if (ec) {
      dest.clear();
      return false;
    } else {
      dest = std::string(sv);
      return true;
    }
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
   * A convenience utility wrapper around std::unique_ptr<char[]>
   *
   * 1. Pointer conversion operators return the underlying raw buffers.
   * 2. Numeric conversion operators return the buffer size.
   * 3. Comparison operators compare against the buffer size.
   * 4. The boolean conversion operator returns true if the buffer is nonzero size.
   */
  class unique_buffer {
  public:
    unique_buffer() : m_len(0) {}
    unique_buffer(size_t n) : m_len(n), m_data(new char[n]) {}

    size_t size() const { return m_len; }
    
    explicit operator void*()  const { return m_data.get(); }
    explicit operator char*()  const { return m_data.get(); }

    explicit operator size_t() const { return m_len; }
    
    bool operator <(size_t rhs) const { return m_len < rhs; }

    void realloc(size_t n) {
      if (n == m_len) {
	return;
	
      } else if (n == 0) {
	m_len = 0;
	m_data = nullptr;
	
      } else {
	m_len = n;
	m_data = std::unique_ptr<char[]>(new char[n]);
      }
    }

    void reset() { realloc(0); }

    /*
    void decode(detector_config_t::compress_t codec,
		const void src, size_t src_len) {
      switch (codec) {
      case bslz4:
	bslz4_decode(src, src_len);
	break;
      case lz4:
	lz4_decode(src, src_len);
	break;
      case none:
	memcpy(m_data.get(), src, src_len);
	break;
      default:
	std::stringstream ss;
	ss << "Error in unique_buffer::decode() : codec " << codec
	   << " unsupported" << std::endl;
	throw std::runtime_error(ss.str());
      }
    }

    void bslz4_decode(const void* cbuf, size_t compressed_size) {
      int64_t decomp_result = bshuf_decompress_lz4(data, m_data.get(), len, sizeof(int), 0);
      if (decomp_result < 0) {
	std::stringstream ss;
	ss << "bshuf_decompress_lz4() failed with status " << decomp_result << std::endl;
	throw std::runtime_error(ss.str());
      }
    }

    void lz4_decode(const void* cbuf, size_t compressed_size) {
      int64_t decomp_result = LZ4_decompress_safe(static_cast<const char*>(data),
						  static_cast<char*>(m_data.get()),
						  len, decompressed_size);
      if (decomp_result < 0) {
	std::stringstream ss;
	ss << "LZ4_decompress_safe() failed with status " << decomp_result << std::endl;
	throw std::runtime_error(ss.str());
      }
      break;
    }
    */
    
  private:
    size_t m_len;
    std::unique_ptr<char[]> m_data;
  };
}

#endif
