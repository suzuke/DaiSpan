//! NVS-backed configuration manager.
//!
//! Stores WiFi credentials and other settings in the ESP32's
//! non-volatile storage (NVS) partition.

use esp_idf_svc::nvs::{EspNvs, EspNvsPartition, NvsDefault};
use esp_idf_svc::sys::EspError;

const NAMESPACE: &str = "daispan3";
const KEY_WIFI_SSID: &str = "wifi_ssid";
const KEY_WIFI_PASS: &str = "wifi_pass";

/// NVS-backed configuration store.
pub struct Config {
    nvs: EspNvs<NvsDefault>,
}

impl Config {
    /// Open (or create) the "daispan3" NVS namespace.
    pub fn new(partition: EspNvsPartition<NvsDefault>) -> Result<Self, EspError> {
        let nvs = EspNvs::new(partition, NAMESPACE, true)?;
        log::info!("Config: NVS namespace '{}' opened", NAMESPACE);
        Ok(Self { nvs })
    }

    /// Read the stored WiFi SSID, if any.
    pub fn get_wifi_ssid(&self) -> Option<String> {
        let mut buf = [0u8; 33];
        match self.nvs.get_str(KEY_WIFI_SSID, &mut buf) {
            Ok(Some(s)) => {
                let s = s.trim_end_matches('\0');
                if s.is_empty() {
                    None
                } else {
                    Some(s.to_string())
                }
            }
            _ => None,
        }
    }

    /// Read the stored WiFi password, if any.
    pub fn get_wifi_password(&self) -> Option<String> {
        let mut buf = [0u8; 65];
        match self.nvs.get_str(KEY_WIFI_PASS, &mut buf) {
            Ok(Some(s)) => {
                let s = s.trim_end_matches('\0');
                if s.is_empty() {
                    None
                } else {
                    Some(s.to_string())
                }
            }
            _ => None,
        }
    }

    /// Store WiFi SSID and password in NVS.
    pub fn set_wifi_credentials(&mut self, ssid: &str, password: &str) -> Result<(), EspError> {
        self.nvs.set_str(KEY_WIFI_SSID, ssid)?;
        self.nvs.set_str(KEY_WIFI_PASS, password)?;
        log::info!("Config: WiFi credentials saved (SSID={})", ssid);
        Ok(())
    }
}
