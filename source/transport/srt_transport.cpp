#include "srt_transport.h"
#include <format>
#include <chrono>

srt_transport::srt_transport()
{
}

srt_transport::~srt_transport()
{
  stop();
}

bool srt_transport::initialize(const stream_config& config)
{
  m_config = config;
  m_stats.stream_id = config.id;
  m_stats.protocol = transport_protocol::srt;
  log_message("SRT transport initialized");
  return true;
}

bool srt_transport::start()
{
  if (m_running.load()) {
    return true;
  }

  m_running = true;
  m_connected = false;
  m_socket_ready = false;

  // Note: SRT library initialization would happen here
  // srt_startup() call

  log_message(std::format("SRT transport starting to {}", m_config.address));

  m_threads.emplace_back([this]() { worker_thread_func(); });
  m_threads.emplace_back([this]() { stats_thread_func(); });

  return true;
}

void srt_transport::stop()
{
  if (!m_running.load()) {
    return;
  }

  m_running = false;

  if (m_socket_fd >= 0) {
    // srt_close(m_socket_fd) would be called here
    m_socket_fd = -1;
  }

  for (auto& t : m_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  m_threads.clear();

  m_connected = false;
  m_socket_ready = false;

  log_message("SRT transport stopped");
}

void srt_transport::send_buffer(shared_buffer_ptr buffer, send_callback callback)
{
  if (!m_running.load() || !buffer) {
    if (callback) {
      callback(false, "Transport not running or invalid buffer");
    }
    return;
  }

  if (m_socket_fd < 0) {
    if (callback) {
      callback(false, "Socket not initialized");
    }
    return;
  }

  if (buffer->size > 0 && buffer->data) {
    // SRT send call would go here
    // srt_send(m_socket_fd, buffer->data.get(), buffer->size);
  }

  if (callback) {
    callback(true, "");
  }
}

bool srt_transport::is_connected() const
{
  return m_connected.load();
}

std::string srt_transport::get_stream_id() const
{
  return m_config.id;
}

transport_protocol srt_transport::get_protocol() const
{
  return transport_protocol::srt;
}

stream_stats srt_transport::get_stats() const
{
  std::lock_guard<std::mutex> lock(m_stats_mutex);
  return m_stats;
}

void srt_transport::stats_thread_func()
{
  while (m_running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.last_update = std::chrono::steady_clock::now();
    m_stats.connected = m_connected.load();

    notify_stats(m_stats);
  }
}

void srt_transport::worker_thread_func()
{
  while (m_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Connection management would happen here
    // For now, simulate connection state
    if (!m_connected.load() && m_socket_fd < 0) {
      // Attempt connection logic would go here
    }
  }
}
