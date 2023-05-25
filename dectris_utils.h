#ifndef BP_DECTRIS_UTILS_H
#define BP_DECTRIS_UTILS_H

#include <math.h>
#include <simdjson.h>
#include "bigpicture_utils.h"

namespace bigpicture {

  /**
   * The header_detail field of a stream interface global header, as found in the part 1 message.
   */
  enum class header_detail_t : int {
    unknown=-1,
    none=0,
    basic=2,
    all=3,
  };
  extern std::unordered_map<header_detail_t, std::string> header_detail_names;

  /**
   * The legal values of the "compression" config parameter.
   */
  enum compress_t : int {
    unknown=-1,
    none=0,
    lz4=1,
    bslz4=2,
  };
  extern std::unordered_map<compress_t, std::string> compress_names;
  
  /**
   * Deserialized fields from the "config" parameters of the "detector" subsystem, 
   * found also in the "part 2" message of the global header message. All member 
   * variables have the same name as the corresponding JSON field name.
   *
   * Useful as a cache for frequently-accessed configuration paramters.
   * @note Not copy-constructible
   */
  class detector_config_t {
  public:
    /// Default constructor
    detector_config_t() {
      reset();
    }
    
    /// Move constructor
    detector_config_t(detector_config_t&& src) :
      beam_center_x(src.beam_center_x),
      beam_center_y(src.beam_center_y),
      bit_depth_image(src.bit_depth_image),
      compression(src.compression),
      count_time(src.count_time),
      countrate_correction_count_cutoff(src.countrate_correction_count_cutoff),
      description(src.description),
      detector_distance(src.detector_distance),
      detector_number(std::move(src.detector_number)),
      frame_time(src.frame_time),
      nimages(src.nimages),
      ntrigger(src.ntrigger),
      omega_start(src.omega_start),
      omega_increment(src.omega_increment),
      sensor_thickness(src.sensor_thickness),
      software_version(std::move(src.software_version)),
      wavelength(src.wavelength),
      x_pixel_size(src.x_pixel_size),
      x_pixels_in_detector(src.x_pixels_in_detector),
      y_pixel_size(src.y_pixel_size),
      y_pixels_in_detector(src.y_pixels_in_detector) {
    }
    
    /// Reset the data to an "uninitialized" state.
    /// @note reset() is idempotent
    void reset() {
      beam_center_x = NAN;
      beam_center_y = NAN;
      bit_depth_image = -1;
      compression = none;
      count_time = NAN;
      countrate_correction_count_cutoff = -1;
      description.clear();
      detector_distance = NAN;
      detector_number.clear();
      frame_time = NAN;
      nimages = -1;
      ntrigger = -1;
      omega_start = NAN;
      omega_increment = NAN;
      sensor_thickness = NAN;
      software_version.clear();
      wavelength = NAN;
      x_pixel_size = NAN;
      x_pixels_in_detector = -1;
      y_pixel_size = NAN;
      y_pixels_in_detector = -1;
    }
    
    /// Populates struct fields by copying values from a simdjson object.
    void reset(const simdjson::dom::object& json);

    std::string to_json();
    
    /*
      TODO: beam_center_x and beam_center_y are floats according to the SIMPLON docs,
      but plausibly one would think they would be integer values.
      
      Ask Dectris for clarification and change them if possible.
    */
    double       beam_center_x; // in pixels
    double       beam_center_y; // in pixels
    int64_t      bit_depth_image; // Should always be 32.
    compress_t   compression;
    double       count_time;
    int64_t      countrate_correction_count_cutoff;
    std::string  description;
    double       detector_distance;
    std::string  detector_number;
    double       frame_time;
    int64_t      nimages;
    int64_t      ntrigger;
    double       omega_start;
    double       omega_increment;
    double       sensor_thickness;
    std::string  software_version;
    double       wavelength;
    double       x_pixel_size;
    int64_t      x_pixels_in_detector;
    double       y_pixel_size;
    int64_t      y_pixels_in_detector;
    
  private:
    detector_config_t(const detector_config_t&) = delete;
  };
  
  /**
   * A generic 2D data buffer used for pixel_mask, flatfield, and the countrate table.
   */
  template<typename T> class mask_t {
  public:
    constexpr mask_t() : width(0), height(0) {}
    constexpr mask_t(mask_t&& src) : width(src.width),
				     height(src.height),
				     data(std::move(src.data)) {
    }
    
    constexpr void reset() {
      width = 0;
      height = 0;
      data.reset(nullptr);
    }
    
    void reset(size_t w, size_t h) {
      assert(w > 0 && h > 0 && "failed precondition");
      width = w;
      height = h;
      data = std::unique_ptr<T[]>(new T[w*h]);
    }
    
    constexpr size_t element_size() const { return sizeof(T); }
    constexpr size_t n_bytes() const { return width * height * element_size(); }
    size_t width;
    size_t height;
    std::unique_ptr<T[]> data;
    
  private:
    mask_t(const mask_t&) = delete; //!< Not copy constructible, uses std::unique_ptr.
  };
  
  
  /**
   * An optional helper class for stream_parser implementations which parses and stores 
   * global data for an image series.
   * 
   * @note This parsing interface need not receive the same level of care with respect to 
   *       optimization because global data is only received and parsed once per series.
   */
  class dectris_global_data {
  public:
    /**
     * Aliases for simdjson dom interface types.
     * @note We use the dom interface instead of ondemand because message parts 
     *       containing JSON are small and self-contained.
     */
    using json_parser = simdjson::dom::parser;
    using json_doc    = simdjson::dom::document;
    using json_obj    = simdjson::dom::object;
    using json_arr    = simdjson::dom::array;
    /// @}

    /**
     * Default constructor
     * @note dectris_global_data is trivially destructible.
     */
    dectris_global_data() {
      reset();
    }

    /**
     * Move constructor
     * @note All internal state is preserved and shall be identical in the 
     *       destination as it was previously in the source.
     */
    dectris_global_data(dectris_global_data&& src) :
      m_parse_state(src.m_parse_state),
      m_parser(std::move(src.m_parser)),
      m_using_header_appendix(src.m_using_header_appendix),
      m_series_id(src.m_series_id),
      m_header_detail(src.m_header_detail),
      m_config_json(std::move(src.m_config_json)),
      m_config(std::move(src.m_config)),
      m_flatfield(std::move(src.m_flatfield)),
      m_pixelmask(std::move(src.m_pixelmask)),
      m_countrate_table(std::move(src.m_countrate_table)),
      m_header_appendix(std::move(src.m_header_appendix)) {
      
#ifndef NDEBUG
      src.reset(); // Make access to a moved object easer to detect in debug builds.
#endif
    }

    /**
     * @return true if all global header data for the current series has been parsed, 
     *         false if more data is expected.
     */
    bool parse(const void* data, size_t len);

    /**
     * De-populate all data fields and reset to their defaults.
     * @note reset() is idempotent.
     * @note reset() is used by the constructor.
     */
    void reset() {
      m_parse_state = parse_state_t::part1;
      // Don't reset m_using_header_appendix, it's set by the config file.
      m_series_id = -1;
      m_header_detail = header_detail_t::unknown;
      m_config_json = json_obj();
      m_config.reset();
      m_flatfield.reset();
      m_pixelmask.reset();
      m_countrate_table.reset();
      m_header_appendix.clear();
    }

    /**
     * @todo This is a kludge. We must send a curl request to the detector to determine 
     *       whether or not to expect an appendix for each image and frame.
     */
    void enable_header_appendix() { m_using_header_appendix = true; }

    /**
     * Each accessor method gets the value of a field defined in the global header messages.
     * \defgroup Accessors
     *
     * @todo Full doxygen comments for each accessor explaining what the data is and where 
     *       it came from.
     * @{
     */
    bool                     using_header_appendix() const { return m_using_header_appendix; }
    
    int64_t                  series_id()       const { return m_series_id; }
    header_detail_t          header_detail()   const { return m_header_detail; }
    const json_obj&          config_json()     const { return m_config_json; }
    const detector_config_t& config()          const { return m_config; }
    const mask_t<float>&     flatfield()       const { return m_flatfield; }
    const mask_t<uint32_t>&  pixelmask()       const { return m_pixelmask; }
    const mask_t<float>&     countrate_table() const { return m_countrate_table; }
    /** @}*/

    ///@{
    /**
     * Parses the specified message "part" for Global Header Data as specified
     * in the "Stream Subsystem" section of the Dectris SIMPLON API manual and 
     * writes the data to libcbf's internal buffers.
     *
     * @note These methods only populate data fields and do not change what 
     *       kind of data the parser should expect next.
     * \defgroup Internal helper methods.
     */
    void parse_part1(const void* data, size_t len);
    void parse_part2(const void* data, size_t len);
    void parse_part3(const void* data, size_t len);
    void parse_part4(const void* data, size_t len);
    void parse_part5(const void* data, size_t len);
    void parse_part6(const void* data, size_t len);
    void parse_part7(const void* data, size_t len);
    void parse_part8(const void* data, size_t len);
    void parse_appendix(const void* data, size_t len);
    ///@}
    
  private:
    dectris_global_data(const dectris_global_data&) = delete;
    
    enum class parse_state_t : int {
      unknown=0,
      part1=1,
      part2=2,
      part3=3,
      part4=4,
      part5=5,
      part6=6,
      part7=7,
      part8=8,
      appendix=9,
      done=10,
    };
    
    parse_state_t     m_parse_state;
    json_parser       m_parser;
    bool              m_using_header_appendix;
    
    /**
     * Data parsed out of messages
     * @{
     */
    int64_t           m_series_id;       //!< Found in part 1
    header_detail_t   m_header_detail;   //!< Found in part 1
    json_obj          m_config_json;     //!< Found in part 2 (header_detail: basic, all)
    detector_config_t m_config;          //!< Found in part 2 (basic, all)
    mask_t<float>     m_flatfield;       //!< Found in part 3 & 4 (all)
    mask_t<uint32_t>  m_pixelmask;       //!< Found in part 5 & 6 (all)
    mask_t<float>     m_countrate_table; //!< Found in part 7 & 8 (all)
    std::string       m_header_appendix; //!< Found in "appendix" message
    /** @}*/
  };
  

}

#endif // header guard
