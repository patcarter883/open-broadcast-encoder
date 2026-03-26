use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int};
use std::sync::Once;

// ============================================================================
// C FFI Type Definitions (matching C ABI from encoder_wrapper.h)
// ============================================================================

#[repr(u8)]
pub enum CInputMode {
  CInputNdi = 0,
  CInputRtsp = 1,
  CInputSrt = 2,
  CInputFile = 3,
}

#[repr(u8)]
pub enum CCodec {
  CCodecH264 = 0,
  CCodecH265 = 1,
  CCodecVp9 = 2,
  CCodecAv1 = 3,
}

#[repr(u8)]
pub enum CEncoder {
  CEncoderNvidia = 0,
  CEncoderAmd = 1,
  CEncoderIntel = 2,
  CEncoderLibx264 = 3,
  CEncoderLibx265 = 4,
}

#[repr(u8)]
pub enum CTransportProtocol {
  CTransportRist = 0,
  CTransportSrt = 1,
  CTransportRtmp = 2,
}

#[repr(C)]
pub struct CInputConfig {
  pub mode: u8,
  pub url: [c_char; 1024],
  pub ndi_name: [c_char; 256],
  pub port: u16,
}

#[repr(C)]
pub struct CEncodeConfig {
  pub codec: u8,
  pub encoder: u8,
  pub width: i32,
  pub height: i32,
  pub bitrate_kbps: i32,
  pub framerate_num: i32,
  pub framerate_den: i32,
  pub preset: [c_char; 64],
}

#[repr(C)]
pub struct CStreamConfig {
  pub stream_id: [c_char; 256],
  pub destination_url: [c_char; 1024],
  pub protocol: u8,
  pub port: i32,
  pub sdp_file: [c_char; 1024],
}

#[repr(C)]
pub struct CStreamStats {
  pub stream_id: [c_char; 256],
  pub packets_sent: i64,
  pub bytes_sent: i64,
  pub packets_lost: i64,
  pub rtt_us: i64,
  pub bitrate_kbps: f64,
}

#[repr(C)]
pub struct CCumulativeStats {
  pub num_streams: i32,
  pub streams: [CStreamStats; 16],
  pub uptime_ms: i64,
}

#[repr(C)]
pub struct CNDIDevice {
  pub id: [c_char; 256],
  pub name: [c_char; 256],
  pub hostname: [c_char; 256],
  pub ip: [c_char; 64],
}

// ============================================================================
// Callback Type Definitions
// ============================================================================

pub type LogCallback = extern "C" fn(*const c_char, *const c_char);
pub type StatsCallback = extern "C" fn(*const CCumulativeStats);
pub type NDIDevicesCallback = extern "C" fn(*const CNDIDevice, i32);

// ============================================================================
// FFI Function Declarations
// ============================================================================

#[link(name = "encoder_wrapper", kind = "dylib")]
extern "C" {
  pub fn encoder_wrapper_init(log_cb: LogCallback, stats_cb: StatsCallback) -> c_int;
  pub fn encoder_wrapper_destroy() -> c_int;

  pub fn encoder_set_input_config(cfg: *const CInputConfig) -> c_int;
  pub fn encoder_set_encode_config(cfg: *const CEncodeConfig) -> c_int;

  pub fn encoder_add_stream(cfg: *const CStreamConfig) -> c_int;
  pub fn encoder_remove_stream(stream_id: *const c_char) -> c_int;

  pub fn encoder_start(on_encode_log: LogCallback) -> c_int;
  pub fn encoder_stop() -> c_int;
  pub fn encoder_set_bitrate(bitrate_kbps: i32) -> c_int;

  pub fn encoder_get_cumulative_stats(out: *mut CCumulativeStats) -> c_int;

  pub fn ndi_refresh_devices(cb: NDIDevicesCallback) -> c_int;

  pub fn encoder_open_sdp_file(path: *const c_char) -> c_int;

  pub fn encoder_start_preview() -> c_int;
  pub fn encoder_stop_preview() -> c_int;
}

// ============================================================================
// Safe Rust Wrappers
// ============================================================================

pub struct EncoderWrapper {
  initialized: bool,
}

impl EncoderWrapper {
  pub fn new() -> Self {
    EncoderWrapper {
      initialized: false,
    }
  }

  pub fn init(&mut self, log_cb: LogCallback, stats_cb: StatsCallback) -> Result<(), String> {
    unsafe {
      let result = encoder_wrapper_init(log_cb, stats_cb);
      if result == 0 {
        self.initialized = true;
        Ok(())
      } else {
        Err(format!("Failed to initialize encoder wrapper: {}", result))
      }
    }
  }

  pub fn destroy(&mut self) -> Result<(), String> {
    if !self.initialized {
      return Ok(());
    }
    unsafe {
      let result = encoder_wrapper_destroy();
      if result == 0 {
        self.initialized = false;
        Ok(())
      } else {
        Err(format!("Failed to destroy encoder wrapper: {}", result))
      }
    }
  }

  pub fn is_initialized(&self) -> bool {
    self.initialized
  }
}

impl Drop for EncoderWrapper {
  fn drop(&mut self) {
    let _ = self.destroy();
  }
}
