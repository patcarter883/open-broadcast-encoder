#include "rist_transport.h"
#include "url/url.h"
#include <format>
#include <chrono>

using std::string;
using homer6::url;

rist_transport::rist_transport()
  : m_sender(new RISTNetSender())
{
  m_sender->statisticsCallback = [this](const rist_stats& stats) {
    this->update_stats(stats);
  };
}

rist_transport::~rist_transport()
{
  stop();
}

bool rist_transport::initialize(const stream_config& config)
{
  m_config = config;
  m_stats.stream_id = config.id;
  m_stats.protocol = transport_protocol::rist;
  return true;
}

bool rist_transport::start()
{
  if (m_running.load()) {
    return true;
  }

  RISTNetSender::RISTNetSenderSettings send_config;
  std::vector<std::tuple<string, int>> interface_list;

  url parsed_url {std::format("rist://{}", m_config.address)};

  for (int i = 0; i < m_config.streams; ++i) {
    string rist_url = std::format(
        "rist://"
        "{}:{}?bandwidth={}&buffer-min={}&buffer-max={}&rtt-min={}&rtt-max={}&"
        "reorder-buffer={}&timing-mode=2",
        parsed_url.getHost(),
        parsed_url.getPort() + (2 * i),
        m_config.bandwidth,
        m_config.buffer_min,
        m_config.buffer_max,
        m_config.rtt_min,
        m_config.rtt_max,
        m_config.reorder_buffer);

    interface_list.emplace_back(rist_url, 0);
  }

  send_config.mLogLevel = RIST_LOG_DEBUG;
  send_config.mProfile = RIST_PROFILE_ADVANCED;

  m_sender->initSender(interface_list, send_config);

  m_running = true;
  m_connected = true;

  m_threads.emplace_back([this]() { stats_thread_func(); });

  log_message("RIST transport started");
  return true;
}

void rist_transport::stop()
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

  m_sender->closeAllClientConnections();
  m_sender->destroySender();

  m_connected = false;
  log_message("RIST transport stopped");
}

void rist_transport::send_buffer(shared_buffer_ptr buffer, send_callback callback)
{
  if (!m_running.load() || !buffer) {
    if (callback) {
      callback(false, "Transport not running or invalid buffer");
    }
    return;
  }

  if (buffer->size > 0 && buffer->data) {
    m_sender->sendData(buffer->data.get(), buffer->size, 0, 0);
  }

  if (callback) {
    callback(true, "");
  }
}

bool rist_transport::is_connected() const
{
  return m_connected.load();
}

std::string rist_transport::get_stream_id() const
{
  return m_config.id;
}

transport_protocol rist_transport::get_protocol() const
{
  return transport_protocol::rist;
}

stream_stats rist_transport::get_stats() const
{
  std::lock_guard<std::mutex> lock(m_stats_mutex);
  return m_stats;
}

void rist_transport::stats_thread_func()
{
  while (m_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // Stats are updated via the callback
  }
}

void rist_transport::update_stats(const rist_stats& stats)
{
  std::lock_guard<std::mutex> lock(m_stats_mutex);
  m_stats.bandwidth = stats.stats.sender_peer.bandwidth;
  m_stats.retransmitted_packets = stats.stats.sender_peer.retransmitted;
  m_stats.total_packets = stats.stats.sender_peer.sent;
  m_stats.quality = stats.stats.sender_peer.quality;
  m_stats.rtt = stats.stats.sender_peer.rtt;
  m_stats.last_update = std::chrono::steady_clock::now();
  m_stats.connected = true;

  notify_stats(m_stats);
}
