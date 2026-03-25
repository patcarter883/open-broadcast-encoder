#include "transport_base.h"

void transport_base::set_stats_callback(stats_callback callback)
{
  m_stats_callback = std::move(callback);
}

void transport_base::set_log_callback(std::function<void(const std::string& msg)> callback)
{
  m_log_callback = std::move(callback);
}

void transport_base::notify_stats(const stream_stats& stats)
{
  m_stats = stats;
  if (m_stats_callback) {
    m_stats_callback(stats);
  }
}

void transport_base::log_message(const std::string& msg)
{
  if (m_log_callback) {
    m_log_callback(msg);
  }
}
