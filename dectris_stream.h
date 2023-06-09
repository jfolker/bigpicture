#ifndef BP_DECTRIS_STREAM_H
#define BP_DECTRIS_STREAM_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdint.h>
#include <string>

#include <zmq.hpp>

#include "bigpicture_utils.h"

namespace bigpicture {
  template<typename T> class dectris_streamer;
  
  /**
   * A generic parser of incoming data via the Dectris "Stream" subsystem. The bigpicture 
   * API provides an implementation which converts stream data to miniCBF files, but extending 
   * this interface with another implementation allows for conversion to other output types.
   *
   * Implementations need only implement 2 function with the following signatures, 
   * which returns true upon completion of parsing an image series and false otherwise:
   *
   *   1. bool parse(void*, size*) // returns
   *        Parses the user-specified data and returns true if and only if a complete 
   *        image series has been successfully parsed.
   *      
   *   2. void flush()
   *        Flushes all parsed data to the destination, similar to std::ostream::flush().
   *
   * @tparam Impl A class implementing parse() and flush() functions.
   * @note Implementations may call flush() on themselves to eagerly write out data. 
   *       This interface shall accommodate eager writing.
   * @note For anyone relatively new to C++, wikipedia's page on the curiously-recurring 
   *       template pattern explains the impetus behind this interface. Static polymorphism 
   *       is used in lieu of dynamic polymorphism; this is not for the sake of premature 
   *       optimization, but for the sake of being able to generalize many interfaces from 
   *       one implementation rather than the other way around in a predictable way with 
   *       less boilerplate. Think of this common idiom as "upside-down inheritance".
   */
  template<class Impl> class stream_parser {
  public:
    /**
     * Callable interface which uses a raw buffer as its input.
     * @param data Data buffer.
     * @param len Buffer length in bytes.
     */
    bool operator()(const void* data, size_t len) {
      return static_cast<Impl*>(this)->parse(data, len);
    }
    bool operator()(const unique_buffer& msg) {
      return static_cast<Impl*>(this)->parse(msg.get(), msg.size());
    }
    
    /**
     * Commit all received data to its output source.
     * @warning No way to tell when a complete image series has been processed.
     * @warning May be removed in the future.
     */
    stream_parser<Impl>& flush() {
      static_cast<Impl*>(this)->flush();
      return *(static_cast<Impl*>(this));
    }
    
  protected:
    stream_parser() = default; // Only children can be declared.
    
  private:
    stream_parser(const stream_parser&) = delete; // No copying.
  };

  /**
   * @tparam T stream_parser implementation type.
   */
  template<typename T> class dectris_streamer {
  public:
    /// @param url - The protocol and address of a ZMQ push socket, e.g. tcp://grape.ls-cat.org:9999
    constexpr dectris_streamer(stream_parser<T>& parser, const std::string& url) :
      m_parser(parser),
      m_poll_interval(poll_interval_default),
      m_recv_buf(recv_buf_default),
      m_shutdown_requested(false),
      m_url(url),
      m_zmq_ctx(zmq_nthread_default) {
    }

    constexpr dectris_streamer(stream_parser<T>& parser, const char* url) :
      dectris_streamer(parser, std::string(url)) {
    }

    /// @param config - A deserialized bigpicture config file.
    /// @todo Handle passing the config directly into the streamer
    dectris_streamer(stream_parser<T>& parser, const simdjson::dom::object& config) :
      m_parser(parser),
      m_poll_interval(poll_interval_default),
      m_recv_buf(recv_buf_default),
      m_shutdown_requested(false),
      m_url(url_default),
      m_zmq_ctx(zmq_nthread_default) {
      
      int64_t tmp_int;
      
      if (maybe_extract_json_pointer(tmp_int, config,
				     "/archiver/source/poll_interval")) {
	m_poll_interval = std::chrono::milliseconds(tmp_int * 1000);
      }

      if (maybe_extract_json_pointer(tmp_int, config,
				     "/archiver/source/read_buffer_mb")) {
	m_recv_buf.resize(static_cast<size_t>(tmp_int * 1024 * 1024));
      }

      if (maybe_extract_json_pointer(tmp_int, config,
				     "/archiver/source/workers")) {
	m_zmq_ctx.set(zmq::ctxopt::io_threads, tmp_int);
      }

      maybe_extract_json_pointer(m_url, config,
				 "/archiver/source/zmq_push_socket");
      
      std::clog << "INFO: Initialized dectris_streamer with the following parameters\n"
		<< "  url=\"" << m_url << "\""
		<< "  rcv_buf_size=" << m_recv_buf.size()
		<< "  poll_interval=" << m_poll_interval.count() << "ms" << std::endl;
    }
    
    /**
     * Move constructor
     * @note Required for use by std::thread to avoid passing const refs around.
     */ 
    constexpr dectris_streamer(dectris_streamer&& src) :
      m_parser(std::move(src.m_parser)),
      m_poll_interval(src.m_poll_interval),
      m_recv_buf(std::move(src.m_recv_buf)),
      m_shutdown_requested(src.m_shutdown_requested), 
      m_url(std::move(src.m_url)),
      m_zmq_ctx(std::move(src.m_zmq_ctx)) {
    }
    
    /**
     * A trivial functor wrapper around run().
     * @comment This makes dectris_streamer a Callable, allowing it to be passed 
     *          into the constructor for std::thread.
     */ 
    void operator()() { run(); }
		
    /**
     * Starts the server and runs until shutdown() is called.
     */
    void run() {
      // Setup polling for data. The polling timeout doesn't matter because
      // when we come up empty-handed, we will retry anyway. The poll timeout
      // is tantamount to how many times we want to set an "idle" message to
      // the terminal.
      std::vector<zmq::poller_event<>> in_events(1);
      zmq::poller_t<>     in_poller;
      zmq::socket_t       sock(m_zmq_ctx, zmq::socket_type::pull);
      zmq::mutable_buffer buf(m_recv_buf.get(), m_recv_buf.size());      
            
      in_poller.add(sock, zmq::event_flags::pollin);
      sock.connect(m_url);
      std::clog << "INFO: connected to Dectris DCU at " << m_url << std::endl;
      while (!m_shutdown_requested) {
	// Wait for the start of a new series by polling.
	const auto n_in = in_poller.wait_all(in_events, m_poll_interval);
	if (!n_in) {
	  auto minutes = std::chrono::duration_cast<std::chrono::minutes>(m_poll_interval);
	  std::clog << "INFO: no activity in the past " << minutes.count() << " minutes" << std::endl;
	  continue; // poll again
	}
	
	/*
	  Spin wait for each successive message in the series.
	  This is essential to maintain real-time processing capability. If the DCU
	  is struggling to shovel bytes into its 40-100G NIC fast enough, this will
	  cause us (the consumer) to churn CPU waiting, but it's "less bad" than
	  polling for each message, which adds at least 1 system call, i.e. poll().

	  TODO: Detect when the parser is ready to flush data, and do so below.
	  TODO: Multithread parsing. Receiving and parsing the 2 or 8-part global 
	        header is a "critical section", but once we have it each thread can 
		receive a 4-part image message.
	*/
	bool series_finished = false;
	while (!series_finished) {
	  auto result = in_events[0].socket.recv(buf,zmq::recv_flags::none);
	  if (!result.has_value()) {
	    continue;
	  }
	  series_finished = m_parser(buf.data(), result.value().size);
	}
	std::clog << "INFO: image series successfully committed to storage\n" << std::endl;
	
      } // while not shutting down
    }

    /**
     * Notify the stream client to shutdown in a signal-safe manner.
     * @note The client shall finish processing the current series before termination.
     * @note { This method does not satisfy the legalist's definition of "reentrant".
     *         However, it is most definitely signal-safe in terms of its consequences.
     *         Its action is atomic, it is idempotent, its effect is irreversible.
     */
    void shutdown() { m_shutdown_requested = true; }
    
  private:
    dectris_streamer() = delete;
    dectris_streamer(const dectris_streamer&) = delete;

    static constexpr int64_t poll_interval_default = 60*60*1000; // ms
    static constexpr int64_t recv_buf_default      = 128*1024*1024; // bytes
    static constexpr char    url_default[]         = "tcp://localhost:9999";
    static constexpr int     zmq_nthread_default   = 1;
    
    stream_parser<T>&         m_parser;
    std::chrono::milliseconds m_poll_interval;
    unique_buffer             m_recv_buf;
    std::atomic<bool>         m_shutdown_requested;
    std::string               m_url;
    zmq::context_t            m_zmq_ctx;
  };
  
}

#endif // header guard
