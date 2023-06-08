#include <errno.h>
#include <inttypes.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cbflib/cbf.h>

#include <bitshuffle.h>
#include <lz4.h>

#include "bigpicture_utils.h"
#include "dectris_stream.h"
#include "dectris_utils.h"
#include "stream_to_cbf.h"

using namespace bigpicture;

bool stream_to_cbf::parse(const void* data, size_t len) {
  bool received_series_end = false;
  
  switch (m_parse_state) {
  case parse_state_t::global_header:
    if (m_global.parse(data, len)) {
      m_parse_state = parse_state_t::new_frame;
      m_buffer.resize(sizeof(int) *
		      m_global.config().x_pixels_in_detector *
		      m_global.config().y_pixels_in_detector);
    }
    break;
    
  case parse_state_t::new_frame:
    if (parse_part1_or_series_end(data, len)) {
      // Parsed series end
      received_series_end = true;
      reset(); // sets state to global_header
    } else {
      // Parsed part 1
      build_cbf_header();
      m_parse_state = parse_state_t::midframe_part2;
    }      
    break;
    
  case parse_state_t::midframe_part2:
    parse_part2(data, len);
    m_parse_state = parse_state_t::midframe_part3;
    break;
    
  case parse_state_t::midframe_part3:
    parse_part3(data, len);
    build_cbf_data();
    m_parse_state = parse_state_t::midframe_part4;
    break;
    
  case parse_state_t::midframe_part4:
    parse_part4(data, len);
    if (m_using_image_appendix) {
      m_parse_state = parse_state_t::midframe_appendix;
    } else {
      flush(); // TODO: remove me, call flush() in dectris_streamer
      m_parse_state = parse_state_t::new_frame;
    }
    break;
    
  case parse_state_t::midframe_appendix:
    parse_appendix(data, len);
    flush(); // TODO: remove me, call flush() in dectris_streamer
    m_parse_state = parse_state_t::new_frame;
    break;
    
  default:
    assert(false && "stream_to_cbf is in an unknown state");
    break;
  }

  // Return whether or not we're starting a new series.
  return received_series_end;
}

bool stream_to_cbf::parse_part1_or_series_end(const void* data, size_t len) {
  /*
    As will all other message parts containing json, we are required to copy 
    the data into simdjson's "padded string" machinery in order for it to parse.
    
    Because all our JSON messages are so small, the cost is negligible compared
    to even simdjson's extraordinary parsing speed, and especially relative to 
    optimizations around I/O.
   */
  int64_t series_id;
  std::string htype;
  // simdjson requires us to copy our plain-old json into their padded string construct.
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj json = m_parser.parse(padded).get<json_obj>();
  
  extract_json_value(htype, json, "htype");
  if (htype.compare("dseries_end-1.0") == 0) { // series end
    extract_json_value(series_id, json, "series");
    if (series_id != m_global.series_id()) {
      std::stringstream ss;
      ss << "Invalid series end message, expected series id: " << m_global.series_id()
	 << ", received " << series_id << std::endl;
      throw std::runtime_error(ss.str());
    }
    std::clog << "INFO: series end record - " << padded << std::endl;
    return true;
    
  } else if (htype.compare("dimage-1.0") != 0) { // not part 1
    std::stringstream ss;
    ss << "Expected either a \"dimage-1.0\" (\"Frame Part 1\") or \"dseries_end-1.0\""
       << " (\"End of Series\") message, received \"" << htype << "\"";
    throw std::runtime_error(ss.str());
  }

  // Received a part 1 message
  extract_json_value(m_frame_id, json, "frame");

  /*
    Validate that the series id matches. If the metadata is incorrect for an 
    image, we have no predictable way to find the correct metadata, the entire 
    minicbf is useless.
  */
  extract_json_value(series_id, json, "series");
  if (series_id != m_global.series_id()) {
    std::stringstream ss;
    ss << "Invalid frame part 1 message, expected series id: " << m_global.series_id()
       << ", received " << series_id << std::endl;
    throw std::runtime_error(ss.str());
  }
  
  // Start a new frame and put the global data in first.
  cbf_new_datablock(m_cbf, "image");

  return false;
}

inline void stream_to_cbf::parse_part2(const void* data, size_t len) {
  /*
    Nothing to do except validate message type in debug builds.
    
    We already know the dimensions of our image series from the config 
    parameters.
   */
#ifndef NDEBUG
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();
  validate_htype(record, "dimage_d-1.0");
#endif
}

inline void stream_to_cbf::parse_part3(const void* data, size_t len) {
  m_buffer.decode(m_global.config().compression, data, len);
}

inline void stream_to_cbf::parse_part4(const void* data, size_t len) {
  /*
    Nothing to do except validate message type in debug builds.

    We don't really need the exposure time, start time, and stop time because
    we have the configured exposure time in the global data, and the measured 
    exposure time per image does not vary significantly.
   */
#ifndef NDEBUG
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();
  validate_htype(record, "dconfig-1.0");
#endif
}

inline void stream_to_cbf::parse_appendix(const void* data, size_t len) {
  /*
    This general-purpose class doesn't do anything with the image appendix,
    but future user-specific implementations may use it to do things such
    as determine a specific landing directory and file-naming convention.
  */
  m_appendix = std::string(static_cast<const char*>(data), len);
}

void stream_to_cbf::build_cbf_header() {
  // FIXME: Is it really necessary to convert the pixel size to an integer number?
  // eiger2cbf does it, but surely there's some documentation that can decisively
  // say one way or another whether this is needed or unnecessary loss of precision.
  static const char header_format[] = 
    "\n"
    "# Detector: %s, S/N %s\n"
    "# Pixel_size %" PRId64 "e-6 m x %" PRId64 "e-6 m\n"
    "# Silicon sensor, thickness %.6lf m\n"
    "# Exposure_time %lf s\n"
    "# Exposure_period %lf s\n"
    "# Count_cutoff %" PRId64 " counts\n"
    "# Wavelength %lf A\n"
    "# Detector_distance %lf m\n"
    "# Beam_xy (%d, %d) pixels\n"
    "# Start_angle %lf deg.\n"
    "# Angle_increment %lf deg.\n";
  static char header_content[4096];
  
  const detector_config_t& config = m_global.config();
  snprintf(static_cast<char*>(header_content), sizeof(header_content), header_format,
	   config.description.c_str(), config.detector_number.c_str(),
	   (int64_t)(config.x_pixel_size * 1E6), (int64_t)(config.y_pixel_size * 1E6),
	   config.sensor_thickness,
	   config.count_time,
	   config.frame_time,
	   config.countrate_correction_count_cutoff,
	   config.wavelength,
	   config.detector_distance,
	   (int)config.beam_center_x, (int)config.beam_center_y,
	   config.omega_start + ((double)(m_frame_id-1))*config.omega_increment,
	   config.omega_increment);

  cbf_new_datablock(m_cbf, "image_1");
  cbf_new_category(m_cbf, "array_data");
  cbf_new_column(m_cbf, "header_convention");
  cbf_set_value(m_cbf, "SLS_1.0");
  cbf_new_column(m_cbf, "header_contents");
  cbf_set_value(m_cbf, header_content);

#ifndef NDEBUG
  std::clog << "DEBUG: minicbf header" << header_content << "\n";
#endif
}

inline void stream_to_cbf::build_cbf_data() {
  const detector_config_t& config = m_global.config();
  cbf_new_category(m_cbf, "array_data");
  cbf_new_column(m_cbf, "data");
  cbf_set_integerarray_wdims_fs(m_cbf,
				CBF_BYTE_OFFSET,
				1, // binary id
				m_buffer.get(),
				sizeof(int),
				1, // signed?
				config.x_pixels_in_detector * config.y_pixels_in_detector,
				"little_endian",
				config.x_pixels_in_detector,
				config.y_pixels_in_detector,
				0,
				0); //padding
}

void stream_to_cbf::flush() {
  // Build a filepath and open the output file.
  // TODO: The current implementation litters output files in the cwd of the process.
  //       We need to determine a sufficiently general-purpose directory structure
  //       which is relatively neat and orderly.
  std::stringstream ss_filename;
  ss_filename << m_global.series_id() << "-" << m_frame_id << ".cbf";

  // We open a file handle but pass ownership to libcbf.
  FILE* file_handle = fopen(ss_filename.str().c_str(), "wb");
  if (file_handle == nullptr) {
    std::stringstream ss;
    ss << "libc error: " << ss_filename.str() << " - " << strerror(errno) << "\n";
    throw std::system_error(errno, std::system_category(), ss.str());
  }

  // Write the file; set readable to 1 so we can close the file handle ourselves.
  // NOTE: We do not fclose because cbf_write_file() does it for us.
  int cbf_err = cbf_write_file(m_cbf, file_handle, /*readable*/1, /*format*/CBF,
			       MSG_DIGEST|MIME_HEADERS|PAD_4K, /*encoding*/ENC_BASE64);
  //fclose(file_handle);
  if (cbf_err != 0) {
    std::stringstream ss;
    ss << "libcbf error code " << cbf_err << ": " << ss_filename.str()
       << " - " << cbf_strerror(cbf_err) << "\n";
    throw std::runtime_error(ss.str());
  }
}
