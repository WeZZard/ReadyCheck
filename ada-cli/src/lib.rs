//! ADA CLI shared library
//!
//! Exposes modules shared between multiple binary targets (ada, ada-capture-daemon).

pub mod audio;
pub mod binary_resolver;
pub mod model_manager;

#[doc(hidden)]
pub mod test_utils {
    use std::sync::Mutex;
    /// Global mutex for tests that mutate environment variables.
    /// Environment variables are process-global, so all env-mutating tests
    /// across all modules must serialize through this single lock.
    pub static ENV_MUTEX: Mutex<()> = Mutex::new(());
}
