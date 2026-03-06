mod s21;
mod controller;

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

        // Initialize S21 UART (ESP32-C3 SuperMini: TX=GPIO3, RX=GPIO4)
        let uart = s21::S21Uart::new(
            peripherals.uart1,
            peripherals.pins.gpio3,
            peripherals.pins.gpio4,
        )
        .expect("Failed to initialize S21 UART");

        // Create shared state and controller
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

        log::info!("DaiSpan v3 initialized");

        // Main thread: future HomeKit/web server setup goes here
        loop {
            std::thread::sleep(std::time::Duration::from_secs(5));
            let status = state.read();
            if status.valid {
                log::info!(
                    "Status: power={} mode={:?} target={:.1} current={:.1} fan={:?}",
                    status.power,
                    status.mode,
                    status.target_temp,
                    status.current_temp,
                    status.fan_speed
                );
            } else {
                log::info!("heartbeat (no valid status yet)");
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
