//! Tracer Backend Library
//!
//! This library provides the Rust interface to the native tracer backend
//! components built with Frida.

use std::ffi::CString;
use std::os::raw::{c_char, c_uint};
use std::path::Path;
use std::ptr;

pub mod ffi {
    //! Foreign Function Interface bindings

    // Include auto-generated bindings if available
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(dead_code)]

    // Try to include generated bindings, fall back to manual definitions
    #[cfg(feature = "bindgen")]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

    #[cfg(not(feature = "bindgen"))]
    pub mod manual {
        use std::os::raw::{c_char, c_int, c_uint};

        #[repr(C)]
        pub struct FridaController {
            _private: [u8; 0],
        }

        #[repr(C)]
        #[derive(Debug, Clone, Copy)]
        pub struct TracerStats {
            pub events_captured: u64,
            pub events_dropped: u64,
            pub bytes_written: u64,
            pub active_threads: u32,
            pub hooks_installed: u32,
            pub fallback_events: u64,
        }

        #[repr(C)]
        #[derive(Debug, Clone, Copy, PartialEq)]
        pub enum ProcessState {
            Uninitialized = 0,
            Initialized = 1,
            Spawning = 2,
            Suspended = 3,
            Attaching = 4,
            Attached = 5,
            Running = 6,
            Detaching = 7,
            Failed = 8,
        }

        #[repr(C)]
        #[derive(Debug, Clone, Copy, PartialEq)]
        pub enum FlightRecorderState {
            Idle = 0,
            Armed = 1,
            PreRoll = 2,
            Recording = 3,
            PostRoll = 4,
        }

        extern "C" {
            pub fn frida_controller_create(output_dir: *const c_char) -> *mut FridaController;
            pub fn frida_controller_destroy(controller: *mut FridaController);
            pub fn frida_controller_spawn_suspended(
                controller: *mut FridaController,
                path: *const c_char,
                argv: *const *const c_char,
                out_pid: *mut c_uint,
            ) -> c_int;
            pub fn frida_controller_attach(controller: *mut FridaController, pid: c_uint) -> c_int;
            pub fn frida_controller_detach(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_resume(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_install_hooks(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_arm_trigger(
                controller: *mut FridaController,
                pre_roll_ms: c_uint,
                post_roll_ms: c_uint,
            ) -> c_int;
            pub fn frida_controller_fire_trigger(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_disarm_trigger(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_set_detail_enabled(
                controller: *mut FridaController,
                enabled: c_uint,
            ) -> c_int;
            pub fn frida_controller_start_session(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_stop_session(controller: *mut FridaController) -> c_int;
            pub fn frida_controller_get_stats(controller: *mut FridaController) -> TracerStats;
            pub fn frida_controller_get_state(controller: *mut FridaController) -> ProcessState;
            pub fn frida_controller_get_flight_state(
                controller: *mut FridaController,
            ) -> FlightRecorderState;
        }
    }

    #[cfg(not(feature = "bindgen"))]
    pub use manual::*;
}

use ffi::*;

/// High-level Rust wrapper for the tracer controller
pub struct TracerController {
    ptr: *mut ffi::FridaController,
}

impl TracerController {
    /// Create a new tracer controller
    pub fn new<P: AsRef<Path>>(output_dir: P) -> anyhow::Result<Self> {
        let output_dir = output_dir.as_ref();
        let c_path = CString::new(output_dir.to_str().unwrap())?;

        let ptr = unsafe { ffi::frida_controller_create(c_path.as_ptr()) };

        if ptr.is_null() {
            anyhow::bail!("Failed to create tracer controller");
        }

        Ok(TracerController { ptr })
    }

    /// Spawn a process in suspended state
    pub fn spawn_suspended<P: AsRef<Path>>(
        &mut self,
        path: P,
        args: &[String],
    ) -> anyhow::Result<u32> {
        let path = CString::new(path.as_ref().to_str().unwrap())?;

        // Convert args to C strings
        let c_args: Vec<CString> = args
            .iter()
            .map(|s| CString::new(s.as_str()))
            .collect::<Result<_, _>>()?;

        // Create argv array
        let mut argv: Vec<*const c_char> = c_args.iter().map(|s| s.as_ptr()).collect();
        argv.push(ptr::null());

        let mut pid: c_uint = 0;

        let result = unsafe {
            ffi::frida_controller_spawn_suspended(self.ptr, path.as_ptr(), argv.as_ptr(), &mut pid)
        };

        if result != 0 {
            anyhow::bail!("Failed to spawn process");
        }

        Ok(pid)
    }

    /// Attach to a running process
    pub fn attach(&mut self, pid: u32) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_attach(self.ptr, pid) };

        if result != 0 {
            anyhow::bail!("Failed to attach to process {}", pid);
        }

        Ok(())
    }

    /// Install hooks in the attached process
    pub fn install_hooks(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_install_hooks(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to install hooks");
        }

        Ok(())
    }

    /// Arm flight recorder trigger
    pub fn arm_trigger(&mut self, pre_roll_ms: u32, post_roll_ms: u32) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_arm_trigger(self.ptr, pre_roll_ms, post_roll_ms) };

        if result != 0 {
            anyhow::bail!("Failed to arm flight recorder trigger");
        }

        Ok(())
    }

    /// Fire flight recorder trigger
    pub fn fire_trigger(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_fire_trigger(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to fire flight recorder trigger");
        }

        Ok(())
    }

    /// Disarm flight recorder trigger
    pub fn disarm_trigger(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_disarm_trigger(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to disarm flight recorder trigger");
        }

        Ok(())
    }

    /// Enable or disable the detail lane
    pub fn set_detail_enabled(&mut self, enabled: bool) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_set_detail_enabled(self.ptr, enabled as u32) };

        if result != 0 {
            anyhow::bail!("Failed to update detail lane state");
        }

        Ok(())
    }

    /// Start ATF session output without resuming the process
    pub fn start_session(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_start_session(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to start ATF session");
        }

        Ok(())
    }

    /// Stop ATF session output and finalize files
    pub fn stop_session(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_stop_session(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to stop ATF session");
        }

        Ok(())
    }

    /// Resume a suspended process
    pub fn resume(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_resume(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to resume process");
        }

        Ok(())
    }

    /// Detach from the process
    pub fn detach(&mut self) -> anyhow::Result<()> {
        let result = unsafe { ffi::frida_controller_detach(self.ptr) };

        if result != 0 {
            anyhow::bail!("Failed to detach from process");
        }

        Ok(())
    }

    /// Get current statistics
    pub fn get_stats(&self) -> TracerStats {
        unsafe { ffi::frida_controller_get_stats(self.ptr) }
    }

    /// Get current process state
    pub fn get_state(&self) -> ProcessState {
        unsafe { ffi::frida_controller_get_state(self.ptr) }
    }

    /// Get current flight recorder state
    pub fn get_flight_state(&self) -> FlightRecorderState {
        unsafe { ffi::frida_controller_get_flight_state(self.ptr) }
    }
}

impl Drop for TracerController {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe {
                ffi::frida_controller_destroy(self.ptr);
            }
        }
    }
}

// Ensure thread safety
unsafe impl Send for TracerController {}
unsafe impl Sync for TracerController {}

#[cfg(test)]
mod tests {
    use super::*;
    // Keep only a lightweight sanity test here; all C++ tests run via
    // generated wrappers in tests/tests.rs to avoid duplicate execution.
    #[test]
    fn test_controller_creation() {
        let _ = TracerController::new("./test_output");
    }
}
