//! Controller task module.
//!
//! Owns the S21 UART and manages AC state polling, command dispatch,
//! and error recovery in a dedicated thread.

pub mod state;
#[cfg(target_os = "espidf")]
pub mod task;

pub use state::*;
#[cfg(target_os = "espidf")]
pub use task::Controller;
