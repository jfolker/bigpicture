#include <iostream>
#include <memory>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <thread>

#include <zmq.h>

#include "include/lz4.h"
#include "bigpicture_utils.h"
#include "dectris_utils.h"
#include "dectris_stream.h"
#include "stream_to_cbf.h"

#define BOOST_TEST_MODULE BasicStreamTest
#include <boost/test/unit_test.hpp>

struct global_sequence {
  std::string part1;
  std::string part2;
  // TODO: Add parts 3-8 for testing header_detail=all.
  std::string appendix;
  std::string end;
};

global_sequence global = {
  "{"
  "\"htype\":\"dheader-1.0\","
  "\"series\":1,"
  "\"header_detail\":\"basic\""
  "}", // part1
  "{"
  "\"beam_center_x\":2110,"
  "\"beam_center_y\":2200,"
  "\"bit_depth_image\":32,"
  "\"compression\":\"lz4\","
  "\"count_time\":0.04998900,"
  "\"countrate_correction_count_cutoff\":765063,"
  "\"description\":\"MATTERHORN 2X 65536M\","
  "\"detector_distance\":125.0,"
  "\"detector_number\":\"M-32-0128\","
  "\"frame_time\":0.2,"
  "\"nimages\":1,"
  "\"ntrigger\":2,"
  "\"omega_start\":0.0,"
  "\"omega_increment\":90.0,"
  "\"sensor_thickness\":4.5E-4,"
  "\"software_version\":\"1.8.0\","
  "\"wavelength\":1.670046,"
  "\"x_pixel_size\":7.5E-5,"
  "\"x_pixels_in_detector\":2056,"
  "\"y_pixel_size\":7.5E-5,"
  "\"y_pixels_in_detector\":2181"
  "}", // part2
  "", // apppendix
  "{\"htype\":\"dseries_end-1.0\",\"series\":1}" // series end
};

BOOST_AUTO_TEST_CASE(Basic) {
  using namespace bigpicture;
  
  const std::string addr("tcp://127.0.0.1:9999");
  
  zmq::message_t msg;
  zmq::context_t server_ctx;
  zmq::socket_t  server_sock(server_ctx, zmq::socket_type::push);
  server_sock.bind(addr);
  
  stream_to_cbf parser;
  dectris_streamer<stream_to_cbf> streamer(parser, addr);
  std::thread client_thread(std::ref(streamer));
  
  size_t uncompressed_size = sizeof(int) * 2056 * 2181;
  std::unique_ptr<char[]> uncompressed_image(new char[uncompressed_size]);
  memset(uncompressed_image.get(), 'J', uncompressed_size);

  size_t initial_capacity = uncompressed_size/4;
  std::unique_ptr<char[]> compressed_image(new char[initial_capacity]);
  int compressed_size = LZ4_compress_default(uncompressed_image.get(),
					     compressed_image.get(),
					     uncompressed_size, initial_capacity);

  {
    // Send some messages to the client here
    msg.rebuild(global.part1.data(), global.part1.size());
    server_sock.send(msg, zmq::send_flags::none);

    msg.rebuild(global.part2.data(), global.part2.size());
    server_sock.send(msg, zmq::send_flags::none);

    for (int i=0; i<2; ++i) {
      char strbuf[4096];

      // Part 1
      int size = snprintf(strbuf, sizeof(strbuf),
			  "{\"htype\":\"dimage-1.0\",\"series\":1,\"frame\":%d,"
			  "\"hash\":\"fc67f000d08fe6b380ea9434b8362d22\"}", i);
      msg.rebuild(strbuf, size);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 2
      size = snprintf(strbuf, sizeof(strbuf),
		      "{"
		      "\"htype\":\"dimage_d-1.0\","
		      "\"shape\":[2056,2181],"
		      "\"type\":\"uint32\","
		      "\"encoding\":\"lz4\","
		      "\"size\":%ld}", uncompressed_size);
      msg.rebuild(strbuf, size);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 3
      msg.rebuild(compressed_image.get(), compressed_size);
      server_sock.send(msg, zmq::send_flags::none);

      // Part 4
      size = snprintf(strbuf, sizeof(strbuf),
		      "{\"htype\":\"dconfig-1.0\","
		      "\"start_time\":%lf,"
		      "\"stop_time\":%lf,"
		      "\"real_time\":%lf}",
		      0.5*i, 0.5*(i+1), 0.5);
      msg.rebuild(strbuf, size);
      server_sock.send(msg, zmq::send_flags::none);
    }

    msg.rebuild(global.end.data(), global.end.size());
    server_sock.send(msg, zmq::send_flags::none);
  }

  streamer.shutdown();
  client_thread.join();
}
