use std::env;
use std::path::PathBuf;

fn main() {
  tauri_build::build();

  // Get the profile (dev or release)
  let profile = env::var("PROFILE").unwrap_or_else(|_| "debug".to_string());

  // Determine the CMake build directory
  let cmake_build_dir = if let Ok(build_dir) = env::var("CMAKE_BUILD_DIR") {
    build_dir
  } else {
    format!("../build-{}", profile)
  };

  let encoder_wrapper_lib = PathBuf::from(&cmake_build_dir)
      .join("source")
      .join("encoder_wrapper");

  // Tell Cargo where to find the C++ library
  println!("cargo:rustc-link-search=native={}", encoder_wrapper_lib.display());

  // Link the encoder_wrapper library
  println!("cargo:rustc-link-lib=dylib=encoder_wrapper");

  // Link C++ runtime
  println!("cargo:rustc-link-lib=dylib=stdc++");

  // Set rpath for runtime library discovery
  println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
  println!("cargo:rustc-link-arg=-Wl,-rpath,{}/..", encoder_wrapper_lib.display());

  println!("cargo:rerun-if-changed=../source/encoder_wrapper/encoder_wrapper.h");
}
