mod s21;
mod controller;
#[cfg(target_os = "espidf")]
mod config;
#[cfg(target_os = "espidf")]
mod wifi;
#[cfg(target_os = "espidf")]
mod web;

#[cfg(target_os = "espidf")]
use esp_idf_svc::hal::prelude::Peripherals;
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

        // 2. Initialize WiFi (STA or AP fallback)
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

        // 3. Initialize S21 UART and controller
        let uart = s21::S21Uart::new(
            peripherals.uart1,
            peripherals.pins.gpio3,
            peripherals.pins.gpio4,
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

        // 4. Start web server
        let config_arc = std::sync::Arc::new(std::sync::Mutex::new(cfg));
        let _server = web::start(state.clone(), config_arc, wifi_state.ap_mode)
            .expect("Failed to start web server");

        log::info!("DaiSpan v3 initialized");

        // Keep WiFi state alive and run main loop
        let _wifi = wifi_state;
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

    #[cfg(not(target_os = "espidf"))]
    {
        log::info!("DaiSpan v3 initialized (host mode - no hardware)");
        loop {
            std::thread::sleep(std::time::Duration::from_secs(5));
            log::info!("heartbeat");
        }
    }
}
