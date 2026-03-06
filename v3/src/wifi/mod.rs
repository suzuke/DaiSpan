//! WiFi manager: STA connection with AP fallback.
//!
//! Ported from v2/main/wifi/wifi_manager.c.

pub mod ap;

use std::net::Ipv4Addr;
use std::time::Duration;

use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::modem::WifiModemPeripheral;
use esp_idf_svc::nvs::EspNvsPartition;
use esp_idf_svc::nvs::NvsDefault;
use esp_idf_svc::wifi::{
    AccessPointConfiguration, AuthMethod, BlockingWifi, ClientConfiguration, Configuration,
    EspWifi,
};

use crate::config::Config;

/// Result of WiFi initialization.
pub struct WifiState {
    /// Whether we're in AP mode (true) or STA mode (false).
    pub ap_mode: bool,
    /// IP address (STA: assigned by router, AP: 192.168.4.1).
    pub ip: Ipv4Addr,
    /// The underlying WiFi driver (must be kept alive).
    #[allow(dead_code)]
    wifi: BlockingWifi<EspWifi<'static>>,
}

/// Initialize WiFi: try STA, fallback to AP.
///
/// Takes ownership of the modem peripheral. The returned `WifiState`
/// must be kept alive for the WiFi connection to persist.
pub fn init(
    modem: impl WifiModemPeripheral + 'static,
    sysloop: EspSystemEventLoop,
    nvs: EspNvsPartition<NvsDefault>,
    config: &Config,
) -> Result<WifiState, Box<dyn std::error::Error>> {
    let esp_wifi = EspWifi::new(modem, sysloop.clone(), Some(nvs.clone()))?;
    let mut wifi = BlockingWifi::wrap(esp_wifi, sysloop)?;

    // Check for stored credentials
    let ssid = config.get_wifi_ssid();
    let password = config.get_wifi_password();

    match (ssid, password) {
        (Some(ssid), Some(password)) if !ssid.is_empty() => {
            log::info!("WiFi: attempting STA connection to '{}'", ssid);
            match try_sta(&mut wifi, &ssid, &password) {
                Ok(ip) => {
                    // Lower TX power after connect (~8.5dBm = 34 units of 0.25dBm)
                    unsafe { let _ = esp_idf_svc::sys::esp_wifi_set_max_tx_power(34); }
                    log::info!("WiFi: STA connected, IP={}, low TX power", ip);
                    Ok(WifiState {
                        ap_mode: false,
                        ip,
                        wifi,
                    })
                }
                Err(e) => {
                    log::warn!("WiFi: STA failed ({}), falling back to AP", e);
                    start_ap(&mut wifi)?;
                    let ip = Ipv4Addr::new(192, 168, 4, 1);
                    ap::start_dns_server();
                    Ok(WifiState {
                        ap_mode: true,
                        ip,
                        wifi,
                    })
                }
            }
        }
        _ => {
            log::info!("WiFi: no credentials, starting AP mode");
            start_ap(&mut wifi)?;
            let ip = Ipv4Addr::new(192, 168, 4, 1);
            ap::start_dns_server();
            Ok(WifiState {
                ap_mode: true,
                ip,
                wifi,
            })
        }
    }
}

/// Try STA connection with exponential backoff retries.
fn try_sta(
    wifi: &mut BlockingWifi<EspWifi<'static>>,
    ssid: &str,
    password: &str,
) -> Result<Ipv4Addr, Box<dyn std::error::Error>> {
    let sta_config = ClientConfiguration {
        ssid: ssid
            .try_into()
            .map_err(|_| "SSID too long")?,
        password: password
            .try_into()
            .map_err(|_| "Password too long")?,
        ..Default::default()
    };

    wifi.set_configuration(&Configuration::Client(sta_config))?;
    wifi.start()?;

    // Retry loop with exponential backoff
    const MAX_RETRIES: u32 = 15;
    let mut last_err = String::from("unknown");

    for attempt in 1..=MAX_RETRIES {
        match wifi.connect() {
            Ok(()) => {
                // Wait for IP assignment (60s timeout)
                match wifi.wait_netif_up() {
                    Ok(()) => {
                        let ip_info = wifi.wifi().sta_netif().get_ip_info()?;
                        return Ok(ip_info.ip);
                    }
                    Err(e) => {
                        last_err = format!("netif_up: {}", e);
                        log::warn!("WiFi: wait_netif_up failed on attempt {}: {}", attempt, e);
                    }
                }
            }
            Err(e) => {
                last_err = format!("connect: {}", e);
                log::warn!("WiFi: connect failed ({}/{}): {}", attempt, MAX_RETRIES, e);
            }
        }

        // Exponential backoff: 500ms -> 1s -> 2s, cap at 3s
        let delay_ms = match attempt {
            1..=5 => 500,
            6..=10 => 1000,
            11..=13 => 2000,
            _ => 3000,
        };
        std::thread::sleep(Duration::from_millis(delay_ms));

        // Disconnect before retrying (ignore errors)
        let _ = wifi.disconnect();
    }

    // Stop STA before switching to AP
    let _ = wifi.stop();
    Err(format!("STA failed after {} retries: {}", MAX_RETRIES, last_err).into())
}

/// Start AP mode (APSTA for iPhone compatibility).
fn start_ap(
    wifi: &mut BlockingWifi<EspWifi<'static>>,
) -> Result<(), Box<dyn std::error::Error>> {
    log::info!("WiFi: starting AP mode (DaiSpan-Config)");

    let ap_config = AccessPointConfiguration {
        ssid: "DaiSpan-Config".try_into().map_err(|_| "AP SSID too long")?,
        password: "12345678".try_into().map_err(|_| "AP password too long")?,
        auth_method: AuthMethod::WPA2WPA3Personal,
        channel: 1,
        max_connections: 4,
        ..Default::default()
    };

    // Use APSTA mode for iPhone compatibility
    let sta_config = ClientConfiguration::default();
    wifi.set_configuration(&Configuration::Mixed(sta_config, ap_config))?;
    wifi.start()?;

    // Set TX power for AP mode (13dBm = 52 units of 0.25dBm)
    unsafe { let _ = esp_idf_svc::sys::esp_wifi_set_max_tx_power(52); }

    log::info!("WiFi: AP started (APSTA, ch1, 13dBm)");
    Ok(())
}
