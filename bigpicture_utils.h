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
  /**
   * @return the string representation of the enum value, e.g.
   *         compressor_name(lz4) returns "lz4".
   */
  const std::string& compressor_name(compressor_t x);

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
    unique_buffer(size_t len) : m_len(len), m_data(new char[len]) {}

    constexpr char*  get()  const { return m_data.get(); }
    constexpr size_t size() const { return m_len; }
    
    constexpr explicit operator char*()   const { return get(); }
    constexpr explicit operator void*()   const { return get(); }
    
    constexpr explicit operator int64_t() const { return static_cast<int64_t>(m_len); }
    constexpr explicit operator size_t()  const { return m_len; }
    
    constexpr bool operator <(size_t rhs) const { return m_len < rhs; }
    
    void resize(size_t n) {
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
    
    void reset() { resize(0); }
    
    void decode(compressor_t codec,
		const void* src, size_t src_len) {
      switch (codec) {
      case compressor_t::bslz4:
	bslz4_decode(src, src_len);
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
    
    void bslz4_decode(const void* cbuf, size_t compressed_size) {
      int64_t decomp_result = bshuf_decompress_lz4(cbuf, m_data.get(),
						   compressed_size, sizeof(int), 0);
      if (decomp_result < 0) {
	std::stringstream ss;
	ss << "bshuf_decompress_lz4() failed with status "
	   << decomp_result << std::endl;
	throw std::runtime_error(ss.str());
      } else if (static_cast<size_t>(decomp_result) != m_len) {
	std::stringstream ss;
	ss << "bshuf_decompress_lz4() decompressed "
	   << decomp_result  << " bytes, expected "
	   << m_len << " bytes." << std::endl;
	throw std::runtime_error(ss.str());
      }
    }
    
    void lz4_decode(const void* cbuf, size_t compressed_size) {
      int64_t decomp_result = LZ4_decompress_safe(static_cast<const char*>(cbuf),
						  static_cast<char*>(m_data.get()),
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
    
  private:
    size_t m_len;
    std::unique_ptr<char[]> m_data;
  };
}

#endif
