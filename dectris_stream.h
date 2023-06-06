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

    /**
     * Callable interface which uses a ZeroMQ message as its input.
     * @param msg A ZMQ message containing a message part compliant 
     *            with the Dectris stream interface specification.
     */
    bool operator()(const zmq::message_t& msg) {
      return static_cast<Impl*>(this)->parse(msg.data(), msg.size());
    }

    /**
     * Stream insertion operator (output), similar to std::ostream::operator<< .
     * @param msg A ZMQ message containing a message part compliant 
     *            with the Dectris stream interface specification.
     * @warning No way to tell when a complete record has been processed.
     * @warning May be removed in the future. 
     */
    stream_parser<Impl>& operator<<(zmq::message_t& msg) {
      static_cast<Impl*>(this)->parse(msg.data(), msg.size());
      return *(static_cast<Impl*>(this));
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
  };

  /**
   * @tparam T stream_parser implementation type.
   */
  template<typename T> class dectris_streamer {
  public:
    /// A function which takes as input, a buffer and its size and returns true
    /// when an entire image series has been processed.
    using parser_callback = std::function<bool(const void*, size_t)>;

    /// @param url - The protocol and address of a ZMQ push socket, e.g. tcp://grape.ls-cat.org:9999
    constexpr dectris_streamer(stream_parser<T>& parser, const std::string& url) :
      m_parser(parser),
      m_poll_interval(60*60*1000),
      m_recv_buf_size(128*1024*1024),
      m_recv_buf(new char[m_recv_buf_size]),
      m_shutdown_requested(false),
      m_url(url),
      m_zmq_ctx(1) {
    }

    constexpr dectris_streamer(stream_parser<T>& parser, const char* url) :
      dectris_streamer(parser, std::string(url)) {
    }

    /// @param config - A deserialized bigpicture config file.
    /// @todo Handle passing the config directly into the streamer
    dectris_streamer(stream_parser<T>& parser, const simdjson::dom::object& config) :
      m_parser(parser),
      m_poll_interval(60*60*1000),
      m_recv_buf_size(128*1024*1024),
      // m_recv_buf gets set in ctor body
      m_shutdown_requested(false),
      m_url("tcp://localhost:9999"),
      m_zmq_ctx(1) {
      
      simdjson::simdjson_result<std::string_view> tmp_str;
      simdjson::simdjson_result<int64_t>          tmp_int;
      
      tmp_str = config.at_pointer("/archiver/source/zmq_push_socket").get_string();
      if (!tmp_str.error()) {
	m_url = std::string(tmp_str.value());
      }
      
      tmp_int = config.at_pointer("/archiver/source/read_buffer_mb").get_int64();
      if (!tmp_int.error()) {
	m_recv_buf_size = static_cast<size_t>(tmp_int.value() * 1024 * 1024);
      }
      m_recv_buf = std::unique_ptr<char[]>(new char[m_recv_buf_size]);

      tmp_int = config.at_pointer("/archiver/source/poll_interval").get_int64();
      if (!tmp_int.error()) {
	m_poll_interval = std::chrono::milliseconds(tmp_int.value()*1000);
      }
      std::clog << "INFO: Initialized dectris_streamer with the following parameters\n"
		<< "  url=\"" << m_url << "\""
		<< "  rcv_buf_size=" << m_recv_buf_size
		<< "  poll_interval=" << m_poll_interval.count() << "ms" << std::endl;
    }
    
    /**
     * Move constructor
     * @note Required for use by std::thread to avoid passing const refs around.
     */ 
    constexpr dectris_streamer(dectris_streamer&& src) :
      m_parser(std::move(src.m_parser)),
      m_poll_interval(src.m_poll_interval),
      m_recv_buf_size(src.m_recv_buf_size),
      m_recv_buf(src.m_recv_buf),
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
      zmq::mutable_buffer buf(m_recv_buf.get(), m_recv_buf_size);      
            
      in_poller.add(sock, zmq::event_flags::pollin);
      sock.connect(m_url);
      std::cout << "INFO: connected to Dectris DCU at " << m_url << std::endl;
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

    stream_parser<T>&         m_parser;
    std::chrono::milliseconds m_poll_interval;  // ms
    size_t                    m_recv_buf_size;  // bytes
    std::unique_ptr<char[]>   m_recv_buf;
    std::atomic<bool>         m_shutdown_requested;
    std::string               m_url;
    zmq::context_t            m_zmq_ctx;
  };
  
}

#endif // header guard
