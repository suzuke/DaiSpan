use esp_idf_svc::hal::prelude::Peripherals;
use esp_idf_svc::log::EspLogger;
use esp_idf_svc::sys::link_patches;

fn main() {
    link_patches();
    EspLogger::initialize_default();

    log::info!("DaiSpan v3 starting...");

    let peripherals = Peripherals::take().unwrap();
    let _ = peripherals;

    log::info!("Free heap: {} bytes", unsafe {
        esp_idf_svc::sys::esp_get_free_heap_size()
    });

    log::info!("DaiSpan v3 initialized (skeleton)");
    loop {
        std::thread::sleep(std::time::Duration::from_secs(5));
        log::info!("heartbeat");
    }
}
