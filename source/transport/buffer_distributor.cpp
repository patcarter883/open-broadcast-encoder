#include "buffer_distributor.h"
#include <algorithm>

buffer_distributor::buffer_distributor()
{
}

buffer_distributor::~buffer_distributor()
{
  stop_all();
}

void buffer_distributor::add_transport(transport_ptr transport)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  // Check if already exists
  auto it = std::find_if(m_transports.begin(), m_transports.end(),
      [&transport](const transport_ptr& t) {
        return t->get_stream_id() == transport->get_stream_id();
      });

  if (it != m_transports.end()) {
    return;  // Already exists
  }

  // Set up stats callback to notify on connection changes
  transport->set_stats_callback([this, transport](const stream_stats& stats) {
    if (m_connection_callback) {
      m_connection_callback(transport->get_stream_id(), stats.connected);
    }
  });

  m_transports.push_back(transport);
}

bool buffer_distributor::remove_transport(const std::string& stream_id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = std::find_if(m_transports.begin(), m_transports.end(),
      [&stream_id](const transport_ptr& t) {
        return t->get_stream_id() == stream_id;
      });

  if (it == m_transports.end()) {
    return false;
  }

  (*it)->stop();
  m_transports.erase(it);
  return true;
}

buffer_distributor::transport_ptr buffer_distributor::get_transport(const std::string& stream_id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = std::find_if(m_transports.begin(), m_transports.end(),
      [&stream_id](const transport_ptr& t) {
        return t->get_stream_id() == stream_id;
      });

  if (it != m_transports.end()) {
    return *it;
  }
  return nullptr;
}

std::vector<buffer_distributor::transport_ptr> buffer_distributor::get_transports() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_transports;
}

void buffer_distributor::distribute(transport_base::shared_buffer_ptr buffer)
{
  if (!buffer) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& transport : m_transports) {
    if (transport->is_connected()) {
      transport->send_buffer(buffer);
    }
  }
}

void buffer_distributor::distribute_legacy(const buffer_data& buf)
{
  auto shared = std::make_shared<shared_buffer>();
  shared->data = std::shared_ptr<uint8_t[]>(
      std::make_unique<uint8_t[]>(buf.buf_size).release());
  std::copy(buf.buf_data, buf.buf_data + buf.buf_size, shared->data.get());
  shared->size = buf.buf_size;
  shared->seq = buf.seq;
  shared->ts_ntp = buf.ts_ntp;
  distribute(shared);
}

void buffer_distributor::set_connection_callback(connection_callback callback)
{
  m_connection_callback = std::move(callback);
}

bool buffer_distributor::has_connected_transport() const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& transport : m_transports) {
    if (transport->is_connected()) {
      return true;
    }
  }
  return false;
}

size_t buffer_distributor::transport_count() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_transports.size();
}

void buffer_distributor::start_all()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_running = true;

  for (const auto& transport : m_transports) {
    transport->start();
  }
}

void buffer_distributor::stop_all()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_running = false;

  for (const auto& transport : m_transports) {
    transport->stop();
  }
}
