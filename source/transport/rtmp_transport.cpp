#include "rtmp_transport.h"
#include <format>
#include <chrono>

rtmp_transport::rtmp_transport()
{
}

rtmp_transport::~rtmp_transport()
{
  stop();
}

bool rtmp_transport::initialize(const stream_config& config)
{
  m_config = config;
  m_stats.stream_id = config.id;
  m_stats.protocol = transport_protocol::rtmp;
  log_message("RTMP transport initialized");
  return true;
}

bool rtmp_transport::start()
{
  if (m_running.load()) {
    return true;
  }

  m_running = true;
  m_connected = false;

  log_message(std::format("RTMP transport starting to {}", m_config.address));

  m_threads.emplace_back([this]() { worker_thread_func(); });
  m_threads.emplace_back([this]() { stats_thread_func(); });

  return true;
}

void rtmp_transport::stop()
{
  if (!m_running.load()) {
    return;
  }

  m_running = false;

  for (auto& t : m_threads) {
    if (t.joinable()) {
      t.join();
    }
  }
  m_threads.clear();

  m_connected = false;

  log_message("RTMP transport stopped");
}

void rtmp_transport::send_buffer(shared_buffer_ptr buffer, send_callback callback)
{
  if (!m_running.load() || !buffer) {
    if (callback) {
      callback(false, "Transport not running or invalid buffer");
    }
    return;
  }

  if (!m_connected.load()) {
    if (callback) {
      callback(false, "Not connected");
    }
    return;
  }

  if (buffer->size > 0 && buffer->data) {
    // RTMP send call would go here
    // This would typically go through a GStreamer rtmp2sink element
  }

  if (callback) {
    callback(true, "");
  }
}

bool rtmp_transport::is_connected() const
{
  return m_connected.load();
}

std::string rtmp_transport::get_stream_id() const
{
  return m_config.id;
}

transport_protocol rtmp_transport::get_protocol() const
{
  return transport_protocol::rtmp;
}

stream_stats rtmp_transport::get_stats() const
{
  std::lock_guard<std::mutex> lock(m_stats_mutex);
  return m_stats;
}

void rtmp_transport::stats_thread_func()
{
  while (m_running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.last_update = std::chrono::steady_clock::now();
    m_stats.connected = m_connected.load();

    notify_stats(m_stats);
  }
}

void rtmp_transport::worker_thread_func()
{
  while (m_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // RTMP connection management would happen here
    // RTMP connections typically go through a GStreamer pipeline
    // e.g., rtmp2sink with URL like rtmp://server/live/stream_key
  }
}
