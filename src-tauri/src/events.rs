use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct LogEvent {
  pub level: String,
  pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct StatsEvent {
  pub num_streams: i32,
  pub uptime_ms: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct StreamStatusEvent {
  pub stream_id: String,
  pub connected: bool,
  pub bitrate_kbps: f64,
  pub packets_sent: i64,
  pub packets_lost: i64,
}
