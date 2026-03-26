use serde::{Deserialize, Serialize};
use tauri::State;
use crate::ffi::*;
use crate::state::AppState;
use std::ffi::CString;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct InputConfig {
  pub mode: u8,
  pub url: String,
  pub ndi_name: String,
  pub port: u16,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct EncodeConfig {
  pub codec: u8,
  pub encoder: u8,
  pub width: i32,
  pub height: i32,
  pub bitrate_kbps: i32,
  pub framerate_num: i32,
  pub framerate_den: i32,
  pub preset: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(crate = "serde")]
pub struct StreamConfig {
  pub stream_id: String,
  pub destination_url: String,
  pub protocol: u8,
  pub port: i32,
  pub sdp_file: String,
}

#[tauri::command]
pub fn init_encoder() -> Result<String, String> {
  extern "C" fn log_callback(level: *const std::os::raw::c_char, msg: *const std::os::raw::c_char) {
    // TODO: Forward to frontend via events
  }

  extern "C" fn stats_callback(_stats: *const CCumulativeStats) {
    // TODO: Forward to frontend via events
  }

  let result = unsafe { encoder_wrapper_init(log_callback, stats_callback) };
  if result == 0 {
    Ok("Encoder initialized".to_string())
  } else {
    Err(format!("Failed to initialize encoder: {}", result))
  }
}

#[tauri::command]
pub fn destroy_encoder() -> Result<String, String> {
  let result = unsafe { encoder_wrapper_destroy() };
  if result == 0 {
    Ok("Encoder destroyed".to_string())
  } else {
    Err(format!("Failed to destroy encoder: {}", result))
  }
}

#[tauri::command]
pub fn set_input_config(config: InputConfig) -> Result<String, String> {
  let mut c_config = CInputConfig {
    mode: config.mode,
    url: [0; 1024],
    ndi_name: [0; 256],
    port: config.port,
  };

  let url_bytes = config.url.as_bytes();
  c_config.url[..url_bytes.len().min(1023)].copy_from_slice(&url_bytes[..url_bytes.len().min(1023)]);

  let ndi_bytes = config.ndi_name.as_bytes();
  c_config.ndi_name[..ndi_bytes.len().min(255)].copy_from_slice(&ndi_bytes[..ndi_bytes.len().min(255)]);

  let result = unsafe { encoder_set_input_config(&c_config) };
  if result == 0 {
    Ok("Input config set".to_string())
  } else {
    Err(format!("Failed to set input config: {}", result))
  }
}

#[tauri::command]
pub fn set_encode_config(config: EncodeConfig) -> Result<String, String> {
  let mut c_config = CEncodeConfig {
    codec: config.codec,
    encoder: config.encoder,
    width: config.width,
    height: config.height,
    bitrate_kbps: config.bitrate_kbps,
    framerate_num: config.framerate_num,
    framerate_den: config.framerate_den,
    preset: [0; 64],
  };

  let preset_bytes = config.preset.as_bytes();
  c_config.preset[..preset_bytes.len().min(63)].copy_from_slice(&preset_bytes[..preset_bytes.len().min(63)]);

  let result = unsafe { encoder_set_encode_config(&c_config) };
  if result == 0 {
    Ok("Encode config set".to_string())
  } else {
    Err(format!("Failed to set encode config: {}", result))
  }
}

#[tauri::command]
pub fn add_stream(config: StreamConfig) -> Result<String, String> {
  let mut c_config = CStreamConfig {
    stream_id: [0; 256],
    destination_url: [0; 1024],
    protocol: config.protocol,
    port: config.port,
    sdp_file: [0; 1024],
  };

  let id_bytes = config.stream_id.as_bytes();
  c_config.stream_id[..id_bytes.len().min(255)].copy_from_slice(&id_bytes[..id_bytes.len().min(255)]);

  let url_bytes = config.destination_url.as_bytes();
  c_config.destination_url[..url_bytes.len().min(1023)].copy_from_slice(&url_bytes[..url_bytes.len().min(1023)]);

  let sdp_bytes = config.sdp_file.as_bytes();
  c_config.sdp_file[..sdp_bytes.len().min(1023)].copy_from_slice(&sdp_bytes[..sdp_bytes.len().min(1023)]);

  let result = unsafe { encoder_add_stream(&c_config) };
  if result == 0 {
    Ok(format!("Stream {} added", config.stream_id))
  } else {
    Err(format!("Failed to add stream: {}", result))
  }
}

#[tauri::command]
pub fn remove_stream(stream_id: String) -> Result<String, String> {
  let c_id = CString::new(stream_id.clone())
      .map_err(|e| format!("Invalid stream ID: {}", e))?;

  let result = unsafe { encoder_remove_stream(c_id.as_ptr()) };
  if result == 0 {
    Ok(format!("Stream {} removed", stream_id))
  } else {
    Err(format!("Failed to remove stream: {}", result))
  }
}

#[tauri::command]
pub fn start_encoder() -> Result<String, String> {
  extern "C" fn log_callback(_level: *const std::os::raw::c_char, _msg: *const std::os::raw::c_char) {
    // TODO: Forward to frontend via events
  }

  let result = unsafe { encoder_start(log_callback) };
  if result == 0 {
    Ok("Encoder started".to_string())
  } else {
    Err(format!("Failed to start encoder: {}", result))
  }
}

#[tauri::command]
pub fn stop_encoder() -> Result<String, String> {
  let result = unsafe { encoder_stop() };
  if result == 0 {
    Ok("Encoder stopped".to_string())
  } else {
    Err(format!("Failed to stop encoder: {}", result))
  }
}

#[tauri::command]
pub fn set_bitrate(bitrate_kbps: i32) -> Result<String, String> {
  let result = unsafe { encoder_set_bitrate(bitrate_kbps) };
  if result == 0 {
    Ok(format!("Bitrate set to {} kbps", bitrate_kbps))
  } else {
    Err(format!("Failed to set bitrate: {}", result))
  }
}

#[tauri::command]
pub fn get_stats() -> Result<String, String> {
  let mut stats = CCumulativeStats {
    num_streams: 0,
    streams: Default::default(),
    uptime_ms: 0,
  };

  let result = unsafe { encoder_get_cumulative_stats(&mut stats) };
  if result == 0 {
    Ok(format!("Stats retrieved: {} streams", stats.num_streams))
  } else {
    Err(format!("Failed to get stats: {}", result))
  }
}

#[tauri::command]
pub fn open_sdp_file(path: String) -> Result<String, String> {
  let c_path = CString::new(path.clone())
      .map_err(|e| format!("Invalid path: {}", e))?;

  let result = unsafe { encoder_open_sdp_file(c_path.as_ptr()) };
  if result == 0 {
    Ok(format!("SDP file opened: {}", path))
  } else {
    Err(format!("Failed to open SDP file: {}", result))
  }
}
