use std::env;
use std::path::{Path, PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=src/");
    println!("cargo:rerun-if-changed=include/");
    println!("cargo:rerun-if-changed=CMakeLists.txt");
    
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let profile = env::var("PROFILE").unwrap();
    
    // Determine build type based on profile
    let build_type = match profile.as_str() {
        "debug" => "Debug",
        "release" => "Release",
        _ => "Debug",
    };
    
    // Build the C/C++ components using cmake
    let dst = cmake::Config::new(".")
        .define("CMAKE_BUILD_TYPE", build_type)
        .define("CMAKE_EXPORT_COMPILE_COMMANDS", "ON") // Generate compile_commands.json
        .define("BUILD_TESTING", "OFF") // Don't build C tests during Rust build
        .build();
    
    // Report the location of compile_commands.json for IDE consumption
    let compile_commands_path = dst.join("build").join("compile_commands.json");
    if compile_commands_path.exists() {
        println!("cargo:warning=compile_commands.json generated at: {}", compile_commands_path.display());
        
        // Also create a symlink in a predictable location for easier IDE configuration
        let target_dir = PathBuf::from(env::var("CARGO_TARGET_DIR").unwrap_or_else(|_| "target".to_string()));
        let link_path = target_dir.join("compile_commands.json");
        
        // Remove old symlink if it exists
        let _ = std::fs::remove_file(&link_path);
        
        // Create new symlink (platform-specific)
        #[cfg(unix)]
        {
            if let Err(e) = std::os::unix::fs::symlink(&compile_commands_path, &link_path) {
                println!("cargo:warning=Failed to create compile_commands.json symlink: {}", e);
            } else {
                println!("cargo:warning=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
        
        #[cfg(windows)]
        {
            if let Err(e) = std::os::windows::fs::symlink_file(&compile_commands_path, &link_path) {
                println!("cargo:warning=Failed to create compile_commands.json symlink: {}", e);
            } else {
                println!("cargo:warning=compile_commands.json symlinked to: {}", link_path.display());
            }
        }
    }
    
    // Link the libraries we built
    println!("cargo:rustc-link-search=native={}/lib", dst.display());
    println!("cargo:rustc-link-lib=static=tracer_controller");
    println!("cargo:rustc-link-lib=static=tracer_utils");
    
    // Link Frida libraries - use absolute paths
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let third_parties = manifest_dir.parent().unwrap().join("third_parties");
    println!("cargo:rustc-link-search=native={}/frida-core", third_parties.display());
    println!("cargo:rustc-link-search=native={}/frida-gum", third_parties.display());
    println!("cargo:rustc-link-lib=static=frida-core");
    println!("cargo:rustc-link-lib=static=frida-gum");
    
    // Platform-specific linking
    if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=framework=Foundation");
        println!("cargo:rustc-link-lib=framework=AppKit");
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
        println!("cargo:rustc-link-lib=framework=IOKit");
        println!("cargo:rustc-link-lib=framework=Security");
        println!("cargo:rustc-link-lib=bsm");
        println!("cargo:rustc-link-lib=resolv");
    } else if cfg!(target_os = "linux") {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
        println!("cargo:rustc-link-lib=m");
    }
    
    // Copy the agent library to output directory
    let agent_lib = if cfg!(target_os = "macos") {
        "libfrida_agent.dylib"
    } else if cfg!(target_os = "windows") {
        "frida_agent.dll"
    } else {
        "libfrida_agent.so"
    };
    
    let agent_src = dst.join("lib").join(agent_lib);
    let agent_dst = out_dir.join("../../..").join(agent_lib);
    
    if agent_src.exists() {
        std::fs::copy(&agent_src, &agent_dst).ok();
    }
    
    // Generate bindings (optional - for better Rust integration)
    generate_bindings(&out_dir);
}

fn generate_bindings(out_dir: &Path) {
    // Check if bindgen is available
    if let Ok(output) = std::process::Command::new("bindgen")
        .arg("--version")
        .output()
    {
        if output.status.success() {
            // Generate bindings for our C headers
            let bindings = bindgen::Builder::default()
                .header("include/frida_controller.h")
                .header("include/shared_memory.h")
                .header("include/ring_buffer.h")
                .header("include/tracer_types.h")
                .clang_arg("-I../third_parties/frida-core")
                .clang_arg("-I../third_parties/frida-gum")
                .allowlist_type("FridaController")
                .allowlist_type("SharedMemory")
                .allowlist_type("RingBuffer")
                .allowlist_type("IndexEvent")
                .allowlist_type("DetailEvent")
                .allowlist_type("TracerStats")
                .allowlist_function("frida_controller_.*")
                .allowlist_function("shared_memory_.*")
                .allowlist_function("ring_buffer_.*")
                .derive_default(true)
                .derive_debug(true)
                .generate()
                .expect("Unable to generate bindings");
            
            let bindings_path = out_dir.join("bindings.rs");
            bindings
                .write_to_file(bindings_path)
                .expect("Couldn't write bindings!");
        }
    }
}