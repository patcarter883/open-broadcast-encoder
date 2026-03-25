#include "transport_manager.h"
#include <format>

transport_manager::transport_manager()
{
}

transport_manager::~transport_manager()
{
  stop_all();
}

std::shared_ptr<transport_base> transport_manager::create_transport(const stream_config& config)
{
  std::shared_ptr<transport_base> transport;

  switch (config.protocol) {
    case transport_protocol::rist:
      transport = std::make_shared<rist_transport>();
      break;
    case transport_protocol::srt:
      transport = std::make_shared<srt_transport>();
      break;
    case transport_protocol::rtmp:
      transport = std::make_shared<rtmp_transport>();
      break;
    default:
      return nullptr;
  }

  if (!transport->initialize(config)) {
    return nullptr;
  }

  add_transport(transport);
  return transport;
}

void transport_manager::add_transport(std::shared_ptr<transport_base> transport)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_transports.push_back(transport);
  m_distributor.add_transport(transport);
}

bool transport_manager::remove_transport(const std::string& stream_id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = std::find_if(m_transports.begin(), m_transports.end(),
      [&stream_id](const std::shared_ptr<transport_base>& t) {
        return t->get_stream_id() == stream_id;
      });

  if (it == m_transports.end()) {
    return false;
  }

  (*it)->stop();
  m_distributor.remove_transport(stream_id);
  m_transports.erase(it);
  return true;
}

std::shared_ptr<transport_base> transport_manager::get_transport(const std::string& stream_id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = std::find_if(m_transports.begin(), m_transports.end(),
      [&stream_id](const std::shared_ptr<transport_base>& t) {
        return t->get_stream_id() == stream_id;
      });

  if (it != m_transports.end()) {
    return *it;
  }
  return nullptr;
}

std::vector<std::shared_ptr<transport_base>> transport_manager::get_transports()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_transports;
}

buffer_distributor& transport_manager::get_distributor()
{
  return m_distributor;
}

void transport_manager::start_all()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& transport : m_transports) {
    transport->start();
  }
}

void transport_manager::stop_all()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& transport : m_transports) {
    transport->stop();
  }
}

bool transport_manager::has_connected_transport() const
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& transport : m_transports) {
    if (transport->is_connected()) {
      return true;
    }
  }
  return false;
}

size_t transport_manager::transport_count() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_transports.size();
}
