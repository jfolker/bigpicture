#include <assert.h>
#include <iostream>
#include <stdlib.h>
#include <stdexcept>
#include <string.h>
#include <unordered_map>

#include "bigpicture_utils.h"
#include "dectris_utils.h"

using namespace bigpicture;

bool dectris_global_data::parse(const void* data, size_t len) {  
  switch (m_parse_state) {
  case parse_state_t::part1:
    parse_part1(data, len);
    if (m_header_detail == header_detail_t::all ||
	m_header_detail == header_detail_t::basic) {
      m_parse_state = parse_state_t::part2;

    } else if (m_header_detail == header_detail_t::none) {
      throw std::runtime_error("ERROR: incompatible DCU configuration; header detail is \"none\", "
				   "cannot obtain necessary metadata to process image frames. "
				   "Please set \"header_detail\" to \"all\"");
    } else { // Unreachable, but belt and suspenders
      assert(false && "bad bug in dectris_global_data::parse()");
      std::stringstream ss;
      ss << "Global data parser stuck in unknown state\n"
	 << "\n  m_parse_state=" << (int)m_parse_state 
	 << "\n  m_header_detail=" << (int)m_header_detail
	 << std::endl;
      throw std::runtime_error(ss.str());
    }
    break;
    
  case parse_state_t::part2:
    parse_part2(data, len);
    if (m_header_detail == header_detail_t::basic) {
      m_parse_state = using_header_appendix() ?
	parse_state_t::appendix : parse_state_t::done;
      
    } else if (m_header_detail == header_detail_t::all) {
      m_parse_state = parse_state_t::part3;
      
    } else { // Unreachable, but belt and suspenders
      std::stringstream ss;
      ss << "Global data parser stuck in unknown state\n"
	 << "\n  m_parse_state=" << (int)m_parse_state 
	 << "\n  m_header_detail=" << (int)m_header_detail
	 << std::endl;
      throw std::runtime_error(ss.str());
    }
    break;
  case parse_state_t::part3:
    parse_part3(data, len);
    m_parse_state = parse_state_t::part4;
    break;
  case parse_state_t::part4:
    parse_part4(data, len);
    m_parse_state = parse_state_t::part5;
    break;
  case parse_state_t::part5:
    parse_part5(data, len);
    m_parse_state = parse_state_t::part6;
    break;
  case parse_state_t::part6:
    parse_part6(data, len);
    m_parse_state = parse_state_t::part7;
    break;
  case parse_state_t::part7:
    parse_part7(data, len);
    m_parse_state = parse_state_t::part8;
    break;
  case parse_state_t::part8:
    parse_part8(data, len);
    m_parse_state = using_header_appendix() ?
      parse_state_t::appendix : parse_state_t::done;
    break;
  case parse_state_t::appendix:
    parse_appendix(data, len);
    m_parse_state = parse_state_t::done;
    break;
  case parse_state_t::done:
    m_parse_state = parse_state_t::part1;
    reset();
    break;
  default:
    // Unreachable, but belt and suspenders
    std::stringstream ss;
    ss << "Global data parser stuck in unknown state\n"
       << "\n  m_parse_state=" << (int)m_parse_state 
       << "\n  m_header_detail=" << (int)m_header_detail
       << std::endl;
    throw std::runtime_error(ss.str());
  }
  return (m_parse_state == parse_state_t::done);
}

void dectris_global_data::parse_part1(const void* data, size_t len) {
  simdjson::error_code ec;
  std::string_view tmp_sv;
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();

  // Validate htype for "part 1" even in release builds.
  // The time cost is small and the risk of shenanigans is great..
  validate_htype(record, "dheader-1.0");
  
  record["series"].get<int64_t>().tie(m_series_id, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid value for "
			     "\"series\" in the global header.");
  }

  record["header_detail"].get<std::string_view>().tie(tmp_sv, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid value for "
			     "\"header_detail\" in the global header.");
  }
  m_header_detail = header_detail_value(tmp_sv);
}

inline void dectris_global_data::parse_part2(const void* data, size_t len) {
  // Parse config here.
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  simdjson::dom::object record = m_parser.parse(padded).get<json_obj>();
  m_config.parse(std::cref(record));
#ifndef NDEBUG
  std::clog << "DEBUG: (config for image series)\n"
	    << padded << "\n\n";
#endif
}

void dectris_global_data::parse_part3(const void* data, size_t len) {
  // Flatfield data header
  simdjson::error_code ec;
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();
#ifndef NDEBUG
  validate_htype(record, "dflatfield-1.0");
#endif
  
  int64_t width, height;
  record["shape"].get<json_arr>().at(0).get<int64_t>().tie(width, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid width for the flatfield.");
  }
  record["shape"].get<json_arr>().at(1).get<int64_t>().tie(height, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid height for the flatfield.");
  }
  m_flatfield.reset(width, height);
}

inline void dectris_global_data::parse_part4(const void* data, size_t len) {
  // Flatfield data blob
  if (m_flatfield.n_bytes() != len) {
    std::stringstream ss;
    ss << "Expected flatfield size (bytes): " << m_flatfield.n_bytes()
       << " actual: " << len;
    throw std::runtime_error(ss.str());
  }
  memcpy(m_flatfield.data.get(), data, len);
}

void dectris_global_data::parse_part5(const void* data, size_t len) {
  // Pixelmask data header
  simdjson::error_code ec;
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();
#ifndef NDEBUG
  validate_htype(record, "dpixelmask-1.0");
#endif
  
  int64_t width, height;
  record["shape"].get<json_arr>().at(0).get<int64_t>().tie(width, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid width for the pixel mask.");
  }
  record["shape"].get<json_arr>().at(1).get<int64_t>().tie(height, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid height for the pixel mask.");
  }
  m_pixelmask.reset(width, height);
}

inline void dectris_global_data::parse_part6(const void* data, size_t len) {
  // Pixelmask data blob
  if (m_pixelmask.n_bytes() != len) {
    std::stringstream ss;
    ss << "Expected pixel mask size (bytes): " << m_pixelmask.n_bytes()
       << " actual: " << len;
    throw std::runtime_error(ss.str());
  }
  memcpy(m_pixelmask.data.get(), data, len);
}

void dectris_global_data::parse_part7(const void* data, size_t len) {
  // Countrate table data header
  simdjson::error_code ec;
  simdjson::padded_string padded(static_cast<const char*>(data), len);
  json_obj record = m_parser.parse(padded).get<json_obj>();
#ifndef NDEBUG
  validate_htype(record, "dcountrate_table-1.0");
#endif
  
  int64_t width, height;
  record["shape"].get<json_arr>().at(0).get<int64_t>().tie(width, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid width for the countrate table.");
  }
  record["shape"].get<json_arr>().at(1).get<int64_t>().tie(height, ec);
  if (ec) {
    throw std::runtime_error("The DCU did not provide a valid height for the countrate table.");
  }
  m_countrate_table.reset(width, height);
}

inline void dectris_global_data::parse_part8(const void* data, size_t len) {
  // Countrate table data blob
  if (m_countrate_table.n_bytes() != len) {
    std::stringstream ss;
    ss << "Expected countrate table size (bytes): " << m_countrate_table.n_bytes()
       << " actual: " << len;
    throw std::runtime_error(ss.str());
  }
  memcpy(m_countrate_table.data.get(), data, len);
}

inline void dectris_global_data::parse_appendix(const void* data, size_t len) {
  /*
    We do not use the appendix for anything, but user-specific (lab-specific) 
    code may use it, e.g. for determining a directory structure for image files.
   */
  m_header_appendix = std::string(static_cast<const char*>(data), len);
#ifndef NDEBUG
  std::clog << "DEBUG: (header appendix)\n" << m_header_appendix << "\n\n";
#endif
}

void detector_config_t::parse(const simdjson::dom::object& json) {
  std::string_view tmp_sv;
  simdjson::dom::element tmp_element;

  // Mandatory parameters
  extract_json_value(beam_center_x, json, "beam_center_x");
  extract_json_value(beam_center_y, json, "beam_center_y");
  extract_json_value(bit_depth_image, json, "bit_depth_image");

  extract_json_value(tmp_sv, json, "compression");
  compression = compressor_value(tmp_sv);
  
  extract_json_value(count_time, json, "count_time");
  extract_json_value(countrate_correction_count_cutoff, json,
		     "countrate_correction_count_cutoff");
  extract_json_value(description, json, "description");
  extract_json_value(detector_distance, json, "detector_distance");
  extract_json_value(detector_number, json, "detector_number");
  extract_json_value(frame_time, json, "frame_time");  
  extract_json_value(nimages, json, "nimages");
  extract_json_value(ntrigger, json, "ntrigger");
  extract_json_value(omega_start, json, "omega_start");
  extract_json_value(omega_increment, json, "omega_increment");
  extract_json_value(sensor_thickness, json, "sensor_thickness");
  extract_json_value(software_version, json, "software_version");
  extract_json_value(wavelength, json, "wavelength");
  extract_json_value(x_pixel_size, json, "x_pixel_size");
  extract_json_value(x_pixels_in_detector, json, "x_pixels_in_detector");
  extract_json_value(y_pixel_size, json, "y_pixel_size");
  extract_json_value(y_pixels_in_detector, json, "y_pixels_in_detector");
}

std::string detector_config_t::to_json() const {
  // stringstream is inefficient compared to good old-fashioned snprintf(),
  // but we don't need to keep track of format specifiers, and this method
  // is used to quickly build test cases.
  std::stringstream ss;
  ss << "{"
     << "\"beam_center_x\":"        << beam_center_x        << ","
     << "\"beam_center_y\":"        << beam_center_y        << ","
     << "\"bit_depth_image\":"      << bit_depth_image      << ","
     << "\"compression\":\""        << compressor_name(compression) << "\","
     << "\"count_time\":"           << count_time           << ","
     << "\"countrate_correction_count_cutoff\":" << countrate_correction_count_cutoff << ","
     << "\"description\":\""        << description          << "\","
     << "\"detector_distance\":"    << detector_distance    << ","
     << "\"detector_number\":\""    << detector_number      << "\","
     << "\"frame_time\":"           << frame_time           << ","
     << "\"nimages\":"              << nimages              << ","
     << "\"ntrigger\":"             << ntrigger             << ","
     << "\"omega_start\":"          << omega_start          << ","
     << "\"omega_increment\":"      << omega_increment      << ","
     << "\"sensor_thickness\":"     << sensor_thickness     << ","
     << "\"software_version\":\""   << software_version     << "\","
     << "\"wavelength\":"           << wavelength           << ","
     << "\"x_pixel_size\":"         << x_pixel_size         << ","
     << "\"x_pixels_in_detector\":" << x_pixels_in_detector << ","
     << "\"y_pixel_size\":"         << y_pixel_size         << ","
     << "\"y_pixels_in_detector\":" << y_pixels_in_detector
     << "}";
  
  return ss.str();
}
