mod s21;
mod controller;

/// Pender stub required by embassy-executor (pulled in transitively).
/// We don't use the embassy executor directly — we use block_on() instead.
#[cfg(target_os = "espidf")]
#[no_mangle]
fn __pender(_context: *mut ()) {}
#[cfg(target_os = "espidf")]
mod config;
#[cfg(target_os = "espidf")]
mod wifi;
#[cfg(target_os = "espidf")]
mod web;
#[cfg(all(target_os = "espidf", feature = "matter"))]
mod matter;

#[cfg(target_os = "espidf")]
use esp_idf_svc::hal::peripherals::Peripherals;
#[cfg(target_os = "espidf")]
use esp_idf_svc::log::EspLogger;
#[cfg(target_os = "espidf")]
use esp_idf_svc::sys::link_patches;

fn main() {
    #[cfg(target_os = "espidf")]
    {
        link_patches();
        EspLogger::initialize_default();
    }

    log::info!("DaiSpan v3 starting...");

    #[cfg(target_os = "espidf")]
    {
        let peripherals = Peripherals::take().unwrap();

        log::info!("Free heap: {} bytes", unsafe {
            esp_idf_svc::sys::esp_get_free_heap_size()
        });

        // 1. Initialize NVS config
        let nvs_default = esp_idf_svc::nvs::EspDefaultNvsPartition::take()
            .expect("Failed to take NVS partition");

        let cfg = config::Config::new(nvs_default.clone())
            .expect("Failed to init config");

        // Check if we have WiFi credentials
        let has_wifi_creds = cfg
            .get_wifi_ssid()
            .map(|s| !s.is_empty())
            .unwrap_or(false);

        // Determine boot mode:
        // - With WiFi creds + Matter feature: Matter mode (Matter owns WiFi+BLE)
        // - Without WiFi creds or without Matter: AP-only config mode (our wifi/ module)
        #[cfg(feature = "matter")]
        let use_matter = has_wifi_creds;
        #[cfg(not(feature = "matter"))]
        let use_matter = false;

        if use_matter {
            #[cfg(feature = "matter")]
            run_matter_mode(peripherals, nvs_default, cfg);
        } else {
            run_ap_mode(peripherals, nvs_default, cfg);
        }
    }

    #[cfg(not(target_os = "espidf"))]
    {
        log::info!("DaiSpan v3 initialized (host mode - no hardware)");
        loop {
            std::thread::sleep(std::time::Duration::from_secs(5));
            log::info!("heartbeat");
        }
    }
}

/// AP-only configuration mode.
///
/// Used when no WiFi credentials are stored, or when the `matter` feature
/// is disabled. Our wifi/ module owns the modem and starts an AP for
/// configuration via the web UI.
#[cfg(target_os = "espidf")]
fn run_ap_mode(
    peripherals: Peripherals,
    nvs_default: esp_idf_svc::nvs::EspNvsPartition<esp_idf_svc::nvs::NvsDefault>,
    cfg: config::Config,
) {
    let sysloop = esp_idf_svc::eventloop::EspSystemEventLoop::take()
        .expect("Failed to take system event loop");

    let wifi_state = wifi::init(
        peripherals.modem,
        sysloop,
        nvs_default,
        &cfg,
    )
    .expect("Failed to init WiFi");

    log::info!(
        "WiFi: mode={}, ip={}",
        if wifi_state.ap_mode { "AP" } else { "STA" },
        wifi_state.ip
    );

    // Initialize S21 UART and controller
    #[cfg(feature = "esp32c3")]
    let (tx_pin, rx_pin) = (peripherals.pins.gpio3, peripherals.pins.gpio4);
    #[cfg(feature = "esp32s3")]
    let (tx_pin, rx_pin) = (peripherals.pins.gpio12, peripherals.pins.gpio13);

    let uart = s21::S21Uart::new(
        peripherals.uart1,
        tx_pin,
        rx_pin,
    )
    .expect("Failed to initialize S21 UART");

    let state = controller::SharedState::new();
    let (mut ctrl, _cmd_tx) = controller::Controller::new(uart, state.clone());

    // Spawn controller thread
    std::thread::Builder::new()
        .name("ctrl".into())
        .stack_size(4096)
        .spawn(move || {
            ctrl.init();
            ctrl.run();
        })
        .expect("Failed to spawn controller thread");

    // Start web server
    let config_arc = std::sync::Arc::new(std::sync::Mutex::new(cfg));
    let _server = web::start(state.clone(), config_arc, wifi_state.ap_mode)
        .expect("Failed to start web server");

    log::info!("DaiSpan v3 initialized (AP/STA mode, no Matter)");

    // Keep WiFi state alive and run heartbeat loop
    let _wifi = wifi_state;
    heartbeat_loop(state);
}

/// Matter mode: Matter stack owns WiFi+BLE.
///
/// Used when WiFi credentials are stored and the `matter` feature is enabled.
/// The controller thread runs independently (it only needs UART).
/// Matter handles WiFi, BLE commissioning, mDNS, and the Thermostat/Fan/Swing
/// clusters. The web server still runs alongside for OTA and status.
#[cfg(all(target_os = "espidf", feature = "matter"))]
fn run_matter_mode(
    peripherals: Peripherals,
    nvs_default: esp_idf_svc::nvs::EspNvsPartition<esp_idf_svc::nvs::NvsDefault>,
    cfg: config::Config,
) {
    log::info!("Boot mode: Matter (WiFi credentials found)");

    // Initialize S21 UART and controller (independent of WiFi)
    #[cfg(feature = "esp32c3")]
    let (tx_pin, rx_pin) = (peripherals.pins.gpio3, peripherals.pins.gpio4);
    #[cfg(feature = "esp32s3")]
    let (tx_pin, rx_pin) = (peripherals.pins.gpio12, peripherals.pins.gpio13);

    let uart = s21::S21Uart::new(
        peripherals.uart1,
        tx_pin,
        rx_pin,
    )
    .expect("Failed to initialize S21 UART");

    let state = controller::SharedState::new();
    let (mut ctrl, cmd_tx) = controller::Controller::new(uart, state.clone());

    // Spawn controller thread
    std::thread::Builder::new()
        .name("ctrl".into())
        .stack_size(4096)
        .spawn(move || {
            ctrl.init();
            ctrl.run();
        })
        .expect("Failed to spawn controller thread");

    // Take system event loop and NVS for Matter
    let sysloop = esp_idf_svc::eventloop::EspSystemEventLoop::take()
        .expect("Failed to take system event loop");

    // Start Matter stack in a dedicated thread.
    // Matter takes ownership of the modem (WiFi+BLE) and manages WiFi internally.
    let _matter_handle = matter::start(
        peripherals.modem,
        sysloop,
        nvs_default,
        state.clone(),
        cmd_tx,
    )
    .expect("Failed to start Matter stack");

    // Note: The web server requires WiFi to be connected, which Matter handles.
    // For now, we don't start the web server in Matter mode since Matter owns WiFi.
    // TODO: Once Matter connects WiFi, start web server for OTA on port 8080.

    log::info!("DaiSpan v3 initialized (Matter mode)");

    // Main heartbeat loop
    heartbeat_loop(state);
}

/// Main loop that periodically logs status.
#[cfg(target_os = "espidf")]
fn heartbeat_loop(state: controller::SharedState) -> ! {
    loop {
        std::thread::sleep(std::time::Duration::from_secs(30));
        let status = state.read();
        let heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };
        if status.valid {
            log::info!(
                "heap={} power={} mode={:?} target={:.1} current={:.1}",
                heap,
                status.power,
                status.mode,
                status.target_temp,
                status.current_temp,
            );
        } else {
            log::info!("heap={} (no valid status yet)", heap);
        }
    }
}
