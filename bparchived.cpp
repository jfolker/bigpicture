#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>

#include <hdf5.h>
#include <simdjson.h>
#include <zmq.hpp>

#include "bigpicture_utils.h"
#include "dectris_stream.h"
#include "dectris_utils.h"
#include "stream_to_cbf.h"

// TODO: ZeroMQ doesn't respect our signal handler, and socket reads get
//       interrupted by SIGINT (failing with EINTR).
std::function<void()> shutdown_adapter_func; // I must always be reentrant!
static void signal_handler(int signum) {
  // iostream and printf are not signal-safe (reentrant), but plain-old write() is.
  // So are all the functions called below.
  char strbuf[1024];
  memset(strbuf, '\0', sizeof(strbuf));
  strlcat(strbuf, "bparchived received the \"", sizeof(strbuf));
  strlcat(strbuf, strsignal(signum), sizeof(strbuf));
  strlcat(strbuf, "\" signal. Shutdown will complete after any currently-running "
	  "image series is completed.\n", sizeof(strbuf));
  write(STDOUT_FILENO, strbuf, strlen(strbuf));
  fsync(STDOUT_FILENO); // flush terminal output immediately

  shutdown_adapter_func();
}

static void usage() {
  std::cerr << "bparchived -c <config_file>" << std::endl;
}

int main(int argc, char** argv) {
  using namespace bigpicture;
  std::string config_file("/etc/bigpicture/config.json");
  
  int c = 0;
  int optind = 1;
  while ((c = getopt(argc, argv, "c:")) != -1) {
    ++optind;
    switch (c) {
    case 'c':
      config_file = std::string(optarg);
      ++optind;
      break;
      
    case 'h':
    case '?':
    default:
      usage();
      return 1;
    }
  }

  // Initialize IPC mechanisms with parent process.
  struct sigaction action;
  action.sa_handler = signal_handler;
  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);

  auto& config = load_config_file(config_file);
  stream_to_cbf parser(config);
  dectris_streamer<stream_to_cbf> streamer(std::ref(parser), config);
  shutdown_adapter_func = [&]() { streamer.shutdown(); };
  streamer.run();
  std::clog << "INFO: done" << std::endl;
  
  return 0;
}
