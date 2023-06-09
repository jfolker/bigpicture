#ifndef BP_STREAM_TO_CBF_H
#define BP_STREAM_TO_CBF_H

#include <string>
#include <string.h>
#include <cbflib/cbf.h>
#include <simdjson.h>

#include "dectris_stream.h"
#include "dectris_utils.h"

namespace bigpicture {

  /**
   * Converts data received over Dectris' stream interface into a series of miniCBF files 
   * (1 image per CBF file)
   *
   * @todo stream_to_cbf does not post-process image frames, e.g. by applying a pixel mask.
   *       The pixel mask and any other correction to images must be applied by the DCU.
   *
   * @todo implement move constructor
   *
   */
  class stream_to_cbf : public stream_parser<stream_to_cbf> {
  public:
    using json_parser = simdjson::dom::parser;
    using json_doc    = simdjson::dom::document;
    using json_obj    = simdjson::dom::object;

    /**
     * Default constructor
     */
    stream_to_cbf(bool using_header_appendix=false,
		  bool using_image_appendix=false) :            
      m_cbf(nullptr),
      m_frame_id(-1),
      m_global(using_header_appendix),
      m_parse_state(parse_state_t::global_header),
      m_using_image_appendix(using_image_appendix) {
      
      cbf_make_handle(&m_cbf);
    }
    
    stream_to_cbf(const simdjson::dom::object& config) :            
      m_cbf(nullptr),
      m_frame_id(-1),
      m_global(config),
      m_parse_state(parse_state_t::global_header),
      m_using_image_appendix(false) {
      
      maybe_extract_json_pointer(m_using_image_appendix, config,
				 "/archiver/source/using_image_appendix");
      cbf_make_handle(&m_cbf);
    }

    /**
     * Move constructor
     */
    stream_to_cbf(stream_to_cbf&& src) noexcept :
      m_appendix(std::move(src.m_appendix)),
      m_buffer(std::move(src.m_buffer)),
      m_cbf(src.m_cbf),
      m_frame_id(src.m_frame_id),
      m_global(std::move(src.m_global)),
      m_parser(std::move(src.m_parser)),
      m_parse_state(src.m_parse_state) {
    }

    ~stream_to_cbf() noexcept {
      // TODO: Wrap the CBF handle and CBF processing into its own class.
      // It would be better for stream_to_cbf and all other stream_parser subclasses to be trivially destructible.
      if (m_cbf) cbf_free_handle(m_cbf);
    }
    
    /**
     * Takes in a message part from a dectris stream, parses it, writes a minicbf to a 
     * file every time it has a complete image, and returns false when an entire image 
     * series has been parsed and written out to disk.
     *
     * @return true if there are still more messages to parse, false if we have 
     *              reached the end of an entire image series.
     * @precondition If a pixel mask is used, the pixel mask is applied to all images.
     */
    bool parse(const void* data, size_t len);

    /**
     * Write the parsed data to a minicbf (CBF with only 1 image frame per file).
     * \throws std::system_error
     */
    void flush();

    /**
     * @note This method is idempotent.
     */
    void reset() {
      m_appendix.clear();
      m_buffer.reset();
      m_frame_id = -1;
      m_global.reset();
      // nothing to do for m_parser
      m_parse_state = parse_state_t::global_header;

      cbf_free_handle(m_cbf);
      m_cbf = nullptr; // necessary if line below fails
      cbf_make_handle(&m_cbf);
    }

  private:
    stream_to_cbf(const stream_to_cbf&) = delete;

    void build_cbf_header();
    void build_cbf_data();
    
    /*
      Returns true if message parsed is "End of Series", false if message is 
      "part 1" of a frame, throws std::runtime_error if the message is neither.
    */
    bool parse_part1_or_series_end(const void* data, size_t len);
    void parse_part2(const void* data, size_t len);
    void parse_part3(const void* data, size_t len);
    void parse_part4(const void* data, size_t len);
    void parse_appendix(const void* data, size_t len);

    enum class parse_state_t : int {
      error=0,
      global_header,
      new_frame,
      midframe_part2,
      midframe_part3,
      midframe_part4,
      midframe_appendix,
    };

    // TODO: Separate the logic for parsing and building a CBF, but
    // keep the parsing logic as its own subclass of stream_parser.
    std::string             m_appendix;
    unique_buffer           m_buffer;
    cbf_handle              m_cbf;
    int64_t                 m_frame_id;
    dectris_global_data     m_global;
    json_parser             m_parser;
    parse_state_t           m_parse_state;
    bool                    m_using_image_appendix;
  };
}

#endif // header guard
