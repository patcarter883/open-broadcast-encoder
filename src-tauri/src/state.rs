use std::sync::Mutex;
use crate::ffi::EncoderWrapper;

pub struct AppState {
  pub encoder: Mutex<EncoderWrapper>,
}

impl AppState {
  pub fn new() -> Self {
    AppState {
      encoder: Mutex::new(EncoderWrapper::new()),
    }
  }
}
