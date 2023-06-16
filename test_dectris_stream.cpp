#include <chrono>
#include <iostream>
#include <memory>
#include <stdint.h>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

#include <zmq.h>
#include <lz4.h>

#include "bigpicture_utils.h"
#include "dectris_utils.h"
#include "dectris_stream.h"
#include "stream_to_cbf.h"

#define BOOST_TEST_MODULE DectrisStreamTest
#include <boost/test/unit_test.hpp>

using namespace bigpicture;

struct test_params_t {
  // "countrate table" defaults are plagiarized from the example in the
  // SIMPLON API 1.8.0 manual.
  test_params_t() :
    n_series(1),
    header_detail(header_detail_t::basic),
    countrate_table_width(2),
    countrate_table_height(1000) {
    
    cfg.beam_center_x     = 2110;
    cfg.beam_center_y     = 2200;
    cfg.bit_depth_image   = 32;
    cfg.compression       = compressor_t::lz4;
    cfg.count_time        = 0.2;
    cfg.countrate_correction_count_cutoff = 765063;
    cfg.description       = "MATTERHORN 2X 65536M";
    cfg.detector_distance = 125.0;
    cfg.detector_number   = "M-32-0128";
    cfg.frame_time        = 0.2;
    cfg.nimages           = 1; // images per trigger, total is nimages*ntrigger
    cfg.ntrigger          = 1; 
    cfg.omega_start       = 0.0;
    cfg.omega_increment   = 90.0;
    cfg.sensor_thickness  = 4.5E-4;
    cfg.software_version  = "1.8.0";
    cfg.wavelength        = 1.670046;
    cfg.x_pixel_size      = 7.5E-5;
    cfg.x_pixels_in_detector = 4150;
    cfg.y_pixel_size      = 7.5E-5;
    cfg.y_pixels_in_detector = 4371;
  }

  void log() const {
    std::clog << "n_series=" << n_series << ", "
	      << "header_detail=" << header_detail << ", "
	      << "countrate_table_dimensions=[" << countrate_table_width << ","
	      << countrate_table_height << "],\n"
	      << "config=" << cfg.to_json() << "\n\n";
  }
  
  detector_config_t cfg;
  int               n_series;
  header_detail_t   header_detail;
  int               countrate_table_width;
  int               countrate_table_height;
  std::string       header_appendix;
  std::string       image_appendix;  
};

static int64_t generate_compressed_image(compressor_t codec,
					 const unique_buffer& uncompressed_image,
					 unique_buffer& compressed_image) {
  // It's ok if the buffer is way too big, we just want to confirm that we
  // can decompress images correctly.
  compressed_image.reset(uncompressed_image.size());
  return compressed_image.encode(codec, uncompressed_image.get(), uncompressed_image.size());
}

static void generate_global_part1_message(zmq::message_t& msg,
					  const test_params_t& params,
					  int series_id=0) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dheader-1.0\","
     <<	"\"series\":" << series_id << ","
     << "\"header_detail\":\"" << params.header_detail << "\""
     << "}";
  //std::clog << ss.str();
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_global_part3_message(zmq::message_t& msg,
					  const test_params_t& params) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dflatfield-1.0\","
     << "\"shape\":[" << params.cfg.x_pixels_in_detector
     << "," << params.cfg.y_pixels_in_detector << "],"
     << "\"type\":\"float32\""
     << "}";
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_global_part5_message(zmq::message_t& msg,
					  const test_params_t& params) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dpixelmask-1.0\","
     << "\"shape\":[" << params.cfg.x_pixels_in_detector
     << "," << params.cfg.y_pixels_in_detector << "],"
     << "\"type\":\"uint32\""
     << "}";
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_global_part7_message(zmq::message_t& msg,
					  const test_params_t& params) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dcountrate_table-1.0\","
     << "\"shape\":[" << params.countrate_table_width
     << "," << params.countrate_table_height << "],"
     << "\"type\":\"float32\""
     << "}";
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_series_end_message(zmq::message_t& msg,
					int series_id=0) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dseries_end-1.0\","
     << "\"series\":" << series_id
     << "}";
  //std::clog << ss.str();
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_frame_part1_message(zmq::message_t& msg,
					 int series_id=0,
					 int frame_id=0) {
  // Note: The md5 hash is unused, but if that changes one should be computed.
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dimage-1.0\","
     << "\"series\":" << series_id << ","
     << "\"frame\":" << frame_id << ","
     << "\"hash\":\"fc67f000d08fe6b380ea9434b8362d22\""
     << "}";
  //std::clog << ss.str();
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_frame_part2_message(zmq::message_t& msg,
					 const test_params_t& params,
					 size_t compressed_size) {
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dimage_d-1.0\","
     <<	"\"shape\":[" << params.cfg.beam_center_x << "," << params.cfg.beam_center_y << "],"
     << "\"type\":\"uint" << params.cfg.bit_depth_image << "\","
     << "\"encoding\":\"" << params.cfg.compression << "\","
     << "\"size\":" << compressed_size
     << "}";
  //std::clog << ss.str();
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void generate_frame_part4_message(zmq::message_t& msg,
					 const test_params_t& params,
					 int frame_id=0) {
  using namespace std::chrono;
  // Convert microseconds to nanoseconds.
  nanoseconds real_time(static_cast<int64_t>(params.cfg.frame_time * 1.0E3));
  nanoseconds start_time = real_time * (frame_id-1);
  nanoseconds stop_time  = real_time * frame_id;
  std::stringstream ss;
  ss << "{"
     << "\"htype\":\"dconfig-1.0\","
     << "\"start_time\":" << start_time.count() << ","
     << "\"stop_time\":"  << stop_time.count()  << ","
     << "\"real_time\":"  << real_time.count()
     << "}";
  //std::clog << ss.str();
  msg.rebuild(ss.str().data(), ss.str().size());
}

static void use_tmpdir() {
  char tmpdir_template[] = "/tmp/bigpictureXXXXXX";
  std::string tmpdir(mktemp(tmpdir_template));
  if (tmpdir.empty()) {
    throw std::runtime_error("Failed generate tmpdir for testing");
  } else if (mkdir(tmpdir.c_str(), S_IRWXU|S_IRWXG|S_IRWXO) != 0) {
    std::stringstream ss;
    ss << "mkdir() for tmpdir, " << tmpdir << " failed: "
       << strerror(errno) << std::endl;
    throw std::runtime_error(ss.str());
  } else if (chdir(tmpdir.c_str()) != 0) {
    std::stringstream ss;
    ss << "chdir() to tmpdir, " << tmpdir << " failed: "
       << strerror(errno) << std::endl;
    throw std::runtime_error(ss.str());
  }
  std::clog << "INFO: Using tmpdir - " << tmpdir << "\n";
}

static void run_client_server_pair(const test_params_t& params) {
  const std::string addr("tcp://127.0.0.1:9999");
  params.log();
  use_tmpdir();
  
  zmq::message_t msg;
  zmq::context_t server_ctx;
  zmq::socket_t  server_sock(server_ctx, zmq::socket_type::push);
  server_sock.bind(addr);
  
  stream_to_cbf parser(!params.header_appendix.empty(),
		       !params.image_appendix.empty());
  dectris_streamer<stream_to_cbf> streamer(parser, addr);
  std::thread client_thread(std::ref(streamer));

  // TODO: Consider a way to use real diffraction images here.
  int64_t uncompressed_size = (params.cfg.bit_depth_image/8 *
			       params.cfg.x_pixels_in_detector *
			       params.cfg.y_pixels_in_detector);
  unique_buffer uncompressed_image(uncompressed_size);
  memset(uncompressed_image.get(), 'w', uncompressed_size);

  unique_buffer compressed_image;
  int64_t compressed_size = generate_compressed_image(params.cfg.compression,
						      uncompressed_image,
						      compressed_image);

  unique_buffer flatfield;
  unique_buffer pixel_mask;
  unique_buffer countrate_table;
  if (params.header_detail == header_detail_t::all) {
    flatfield.reset(sizeof(float) *
		    params.cfg.x_pixels_in_detector *
		    params.cfg.y_pixels_in_detector);
    memset(flatfield.get(), 'a', flatfield.size());

    pixel_mask.reset(sizeof(uint32_t) *
		     params.cfg.x_pixels_in_detector *
		     params.cfg.y_pixels_in_detector);
    memset(pixel_mask.get(), 'b', pixel_mask.size());

    countrate_table.reset(sizeof(float) *
			  params.countrate_table_width *
			  params.countrate_table_height);
    memset(countrate_table.get(), 'c', countrate_table.size());
  }

  for (int i=1; i <= params.n_series; ++i) {
    // Send some messages to the client here
    generate_global_part1_message(msg, params, i);
    server_sock.send(msg, zmq::send_flags::none);

    std::string part2 = params.cfg.to_json();
    msg.rebuild(part2.data(), part2.size());
    server_sock.send(msg, zmq::send_flags::none);
    
    if (params.header_detail == header_detail_t::all) {
      // TODO: logic for header_deatil=all goes here.
      // TODO: The copying done here is redundant.
      // Part 3
      generate_global_part3_message(msg, params);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 4
      msg.rebuild(flatfield.get(), flatfield.size());
      server_sock.send(msg, zmq::send_flags::none);

      // Part 5
      generate_global_part5_message(msg, params);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 6
      msg.rebuild(pixel_mask.get(), pixel_mask.size());
      server_sock.send(msg, zmq::send_flags::none);

      // Part 7
      generate_global_part7_message(msg, params);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 8
      msg.rebuild(countrate_table.get(), countrate_table.size());
      server_sock.send(msg, zmq::send_flags::none);
    }

    // Optional header appendix
    if (!params.header_appendix.empty()) {
      msg.rebuild(params.header_appendix.data(), params.header_appendix.size());
      server_sock.send(msg, zmq::send_flags::none);
    }

    int total_images = (int)(params.cfg.ntrigger * params.cfg.nimages);
    for (int j=1; j <= total_images; ++j) {
      // Part 1
      // TODO: Populate hash with a valid MD5, even though it is unused.
      generate_frame_part1_message(msg, /*series*/i, /*frame*/j);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 2
      generate_frame_part2_message(msg, params, compressed_size);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 3 (image)
      // TODO: Consider some way of plumbing in previous-generated real-live
      //       diffraction images here.
      msg.rebuild(compressed_image.get(), compressed_size);
      server_sock.send(msg, zmq::send_flags::none);
      
      // Part 4
      generate_frame_part4_message(msg, params, /*frame*/j);
      server_sock.send(msg, zmq::send_flags::none);
      
      // Optional image appendix
      if (!params.image_appendix.empty()) {
	msg.rebuild(params.image_appendix.data(), params.image_appendix.size());
	server_sock.send(msg, zmq::send_flags::none);
      }
    }
    
    // Series end
    generate_series_end_message(msg, i);
    server_sock.send(msg, zmq::send_flags::none);
  }

  streamer.shutdown();
  client_thread.join();
}

BOOST_AUTO_TEST_SUITE(TestDectrisStream);

BOOST_AUTO_TEST_CASE(no_compression) {
  std::clog << "*** TEST CASE: no_compression ***\n";
  test_params_t params;
  params.cfg.compression = compressor_t::none;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(lz4) {
  std::clog << "******** TEST CASE: lz4 *********\n";
  test_params_t params;
  params.cfg.compression = compressor_t::lz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(bslz4) {
  std::clog << "******* TEST CASE: bslz4 ********\n";
  test_params_t params;
  params.cfg.compression = compressor_t::bslz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(header_appendix) {
  std::clog << "*** TEST CASE: header_appendix ***\n";
  test_params_t params;
  params.cfg.compression = compressor_t::lz4;
  params.header_appendix = "{\"esaf\":\"PER-SERIES LS-CAT ESAF STUFF\"}";
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(image_appendix) {
  std::clog << "*** TEST CASE: image_appendix ***\n";
  test_params_t params;
  params.cfg.compression = compressor_t::lz4;
  params.image_appendix = "{\"esaf\":\"PER-IMAGE LS-CAT ESAF STUFF\"}";
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(header_and_image_appendix) {
  std::clog << "*** TEST CASE: header_and_image_appendix ***\n";
  test_params_t params;
  params.cfg.compression = compressor_t::lz4;
  params.header_appendix = "{\"esaf\":\"PER-SERIES LS-CAT ESAF STUFF\"}";
  params.image_appendix  = "{\"esaf\":\"PER-IMAGE LS-CAT ESAF STUFF\"}";
  run_client_server_pair(params);
  std::clog << "************** END TEST CASE ***************\n\n";
}

BOOST_AUTO_TEST_CASE(header_detail_all) {
  std::clog << "*** TEST CASE: header_detail_all ***\n";
  test_params_t params;
  params.header_detail = header_detail_t::all;
  params.cfg.compression = compressor_t::lz4;
  run_client_server_pair(params);
  std::clog << "************** END TEST CASE ***************\n\n";
}

BOOST_AUTO_TEST_CASE(all_plus_appendix) {
  std::clog << "*** TEST CASE: header_and_image_appendix ***\n";
  test_params_t params;
  params.header_detail = header_detail_t::all;
  params.cfg.compression = compressor_t::lz4;
  params.header_appendix = "{\"esaf\":\"PER-SERIES LS-CAT ESAF STUFF\"}";
  params.image_appendix  = "{\"esaf\":\"PER-IMAGE LS-CAT ESAF STUFF\"}";
  run_client_server_pair(params);
  std::clog << "************** END TEST CASE ***************\n\n";
}

BOOST_AUTO_TEST_CASE(multi_series) {
  std::clog << "******* TEST CASE: multi_series ********\n";
  test_params_t params;
  params.n_series = 4;
  params.cfg.compression = compressor_t::lz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(multi_trigger) {
  std::clog << "******* TEST CASE: multi_trigger ********\n";
  test_params_t params;
  params.cfg.ntrigger = 4;
  params.cfg.compression = compressor_t::lz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

BOOST_AUTO_TEST_CASE(multi_image) {
  std::clog << "******* TEST CASE: multi_image ********\n";
  test_params_t params;
  params.cfg.nimages = 4;
  params.cfg.compression = compressor_t::lz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********\n\n";
}

/*
// TODO: Move this into a separate file, log performance metrics, 
// and run performance tests as a separate Makefile target.
BOOST_AUTO_TEST_CASE(stream_400_images) {
  std::clog << "******* TEST CASE: bslz4 ********\n";
  test_params_t params;
  params.n_series = 2;
  params.cfg.nimages  = 2;
  params.cfg.ntrigger = 100;
  params.cfg.compression = compressor_t::bslz4;
  run_client_server_pair(params);
  std::clog << "********* END TEST CASE *********" << std::endl;
}
*/

BOOST_AUTO_TEST_SUITE_END();
