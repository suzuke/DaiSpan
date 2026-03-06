pub mod s21;
pub mod controller;
#[cfg(target_os = "espidf")]
pub mod config;
#[cfg(target_os = "espidf")]
pub mod wifi;
#[cfg(target_os = "espidf")]
pub mod web;
