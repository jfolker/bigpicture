#include <atomic>
#include <string.h>
#include <execinfo.h>
#include <iostream>
#include <list>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <jxl/encode.h> // libjxl
#include <zmq.hpp>      // cppzmq (a header-only library)

#include "bigpicture_utils.h"

static const int shutdown_timeout = 5;

/**
 * This executable is responsible for spawning, killing, and monitoring all 
 * child processes. 
 */
std::atomic<bool> shutdown = false;
static void signal_handler(int signum) {
  // iostream and printf are not signal-safe (reentrant), but plain-old write() is.
  shutdown = true;
  char strbuf[1024];
  memset(strbuf, '\0', sizeof(strbuf));
  strlcat(strbuf, "Received \"", sizeof(strbuf));
  strlcat(strbuf, strsignal(signum), sizeof(strbuf));
  strlcat(strbuf, "\" signal, shutting down now.\n", sizeof(strbuf));
  write(STDOUT_FILENO, strbuf, strlen(strbuf));
  fsync(STDOUT_FILENO); // flush terminal output immediately
}

static void usage() {
  std::cerr << "  bigpicture [-h] config_file\n"
	    << "    -h : print out usage and exit\n"
	    << "    -c : a JSON-based config file. Default is /etc/bigpicture/config.json" << std::endl;
}

int main(int argc, char** argv) {
  int c = 0;
  int optind = 1;
  std::string config_file("/etc/bigpicture/config.json");

  // parse keyword args
  while ((c = getopt(argc, argv, "hc:")) != -1) {
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

  // Load the config file.
  simdjson::dom::parser parser;
  simdjson::dom::element config;
  auto simdjson_err = parser.load(config_file).get(config);
  if (simdjson_err) {
    std::cerr << simdjson_err << std::endl;
    return 1;
  }
  //std::cout << config << std::endl;

  // Setup IPC and initialize libraries here.
  std::cout << "bigpicture is starting up" << std::endl;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Start child processes
  std::list<pid_t> child_pids;
  
  // Main event loop; monitors all child processes, reports errors,
  // and restarts downed processes.
  //std::cout << "bigpicture is ready" << std::endl;
  std::cout << "bigpicture is ready" << std::endl;
  while (!shutdown) {
    sleep(1);
  }

  // Send SIGTERM to children and give them until the timeout to wrap up.
  // If they don't shutdown, use SIGKILL.
  for (auto pid : child_pids) {
    int proc_status = 0;
    waitpid(pid, &proc_status, WNOHANG);
    kill(pid, SIGTERM);
  }
  sleep(shutdown_timeout);
  for (auto pid : child_pids) {
    int proc_status = 0;
    waitpid(pid, &proc_status, WNOHANG);
    kill(pid, SIGKILL);
  }
  
  return 0;
}
