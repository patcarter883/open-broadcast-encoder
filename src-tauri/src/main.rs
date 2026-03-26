// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod commands;
mod events;
mod ffi;
mod state;

use state::AppState;

fn main() {
  tauri::Builder::default()
      .manage(AppState::new())
      .invoke_handler(
          tauri::generate_handler![
              commands::init_encoder,
              commands::destroy_encoder,
              commands::set_input_config,
              commands::set_encode_config,
              commands::add_stream,
              commands::remove_stream,
              commands::start_encoder,
              commands::stop_encoder,
              commands::set_bitrate,
              commands::get_stats,
              commands::open_sdp_file,
          ]
      )
      .run(tauri::generate_context!())
      .expect("error while running tauri application");
}
