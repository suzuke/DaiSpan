pub mod types;
pub mod codec;
pub mod version;
#[cfg(target_os = "espidf")]
pub mod uart;
#[cfg(target_os = "espidf")]
pub mod commands;

pub use types::*;
pub use codec::{encode_frame, decode_frame, checksum};
pub use version::{S21Version, S21Features};
#[cfg(target_os = "espidf")]
pub use uart::S21Uart;
