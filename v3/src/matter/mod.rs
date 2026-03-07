//! Matter integration module for DaiSpan.
//!
//! Provides Thermostat, Fan Control, and Swing (On/Off) clusters
//! using the esp-idf-matter / rs-matter stack.
//!
//! Architecture:
//! - Matter stack owns WiFi+BLE modem via `EspWifiMatterStack`
//! - Thermostat + Fan clusters use custom `AsyncHandler` impls
//!   (rs-matter has no built-in Thermostat/Fan cluster handlers)
//! - Swing uses the built-in `OnOffHandler` with custom `OnOffHooks`
//! - All handlers read from `SharedState` and send `ControllerCmd` to controller
//!
//! The Matter stack is statically allocated and runs in a dedicated thread
//! with 20KB stack.

pub mod fan;
pub mod swing;
pub mod thermostat;

use core::pin::pin;
use std::sync::mpsc::SyncSender;

use alloc::sync::Arc;

use embassy_sync::blocking_mutex::raw::NoopRawMutex;
use static_cell::StaticCell;

use esp_idf_matter::init_async_io;
use esp_idf_matter::matter::crypto::{default_crypto, Crypto};
use esp_idf_matter::matter::dm::clusters::desc::{self, ClusterHandler as _, DescHandler};
use esp_idf_matter::matter::dm::clusters::on_off::{self, OnOffHandler, OnOffHooks};
use esp_idf_matter::matter::dm::devices::test::{
    DAC_PRIVKEY, TEST_DEV_ATT, TEST_DEV_COMM,
};
use esp_idf_matter::matter::dm::clusters::basic_info::BasicInfoConfig;
use esp_idf_matter::matter::dm::{
    Async, Dataver, EmptyHandler, Endpoint, EpClMatcher, Node,
};
use esp_idf_matter::matter::dm::DeviceType;
use esp_idf_matter::matter::{clusters, devices};
use esp_idf_matter::matter::utils::init::InitMaybeUninit;
use esp_idf_matter::wireless::{EspMatterWifi, EspWifiMatterStack};

use esp_idf_svc::bt::reduce_bt_memory;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::hal::modem::Modem;
use esp_idf_svc::hal::task::block_on;
use esp_idf_svc::io::vfs::MountedEventfs;
use esp_idf_svc::nvs::EspDefaultNvsPartition;
use esp_idf_svc::timer::EspTaskTimerService;

use crate::controller::state::{ControllerCmd, SharedState};

use self::fan::{FanHandler, CLUSTER_FAN_CONTROL, FAN_CONTROL_CLUSTER};
use self::swing::SwingOnOffLogic;
use self::thermostat::{ThermostatHandler, CLUSTER_THERMOSTAT, THERMOSTAT_CLUSTER};

extern crate alloc;

/// Matter stack thread stack size (20KB)
const MATTER_STACK_SIZE: usize = 16 * 1024;

/// Bump allocator size for the Matter stack futures
const BUMP_SIZE: usize = 15000;

/// Endpoint IDs (0 is root, reserved by Matter)
const THERMOSTAT_ENDPOINT_ID: u16 = 1;
const FAN_ENDPOINT_ID: u16 = 2;
const SWING_ENDPOINT_ID: u16 = 3;

/// Matter Thermostat device type (0x0301)
const DEV_TYPE_THERMOSTAT: DeviceType = DeviceType {
    dtype: 0x0301,
    drev: 2,
};

/// Matter Fan device type (0x002B)
const DEV_TYPE_FAN: DeviceType = DeviceType {
    dtype: 0x002B,
    drev: 1,
};

/// DaiSpan device info for Matter commissioning.
const DAISPAN_DEV_DET: BasicInfoConfig = BasicInfoConfig {
    vid: 0xFFF1,   // Test vendor ID (use real VID for production)
    pid: 0x8002,   // Test product ID
    hw_ver: 1,
    hw_ver_str: "3.0",
    sw_ver: 1,
    sw_ver_str: "3.0.0",
    serial_no: "DS3-001",
    product_name: "DaiSpan Thermostat",
    vendor_name: "DaiSpan",
    device_name: "DaiSpan",
    ..BasicInfoConfig::new()
};

/// Static allocation for the Matter stack (mandatory for WifiBle variant).
static MATTER_STACK: StaticCell<EspWifiMatterStack<'static, BUMP_SIZE, ()>> = StaticCell::new();

/// The Matter Node descriptor with all endpoints.
const NODE: Node = Node {
    id: 0,
    endpoints: &[
        // Endpoint 0: Root (system clusters, handled by Matter stack)
        EspWifiMatterStack::<0, ()>::root_endpoint(),
        // Endpoint 1: Thermostat
        Endpoint {
            id: THERMOSTAT_ENDPOINT_ID,
            device_types: devices!(DEV_TYPE_THERMOSTAT),
            clusters: clusters!(DescHandler::CLUSTER, THERMOSTAT_CLUSTER, FAN_CONTROL_CLUSTER),
        },
        // Endpoint 2: Fan (separate endpoint for controllers that want fan-only control)
        Endpoint {
            id: FAN_ENDPOINT_ID,
            device_types: devices!(DEV_TYPE_FAN),
            clusters: clusters!(DescHandler::CLUSTER, FAN_CONTROL_CLUSTER),
        },
        // Endpoint 3: Swing (On/Off switch for vertical swing)
        Endpoint {
            id: SWING_ENDPOINT_ID,
            device_types: devices!(esp_idf_matter::matter::dm::devices::DEV_TYPE_ON_OFF_LIGHT),
            clusters: clusters!(DescHandler::CLUSTER, SwingOnOffLogic::CLUSTER),
        },
    ],
};

/// Start the Matter stack in a dedicated thread.
///
/// This function takes ownership of the modem (WiFi+BLE) and runs
/// the Matter stack. It does NOT return unless Matter crashes.
///
/// # Arguments
/// - `modem` - WiFi+BLE modem peripheral
/// - `sysloop` - ESP-IDF system event loop
/// - `nvs` - NVS partition for Matter persistence
/// - `state` - shared AC status (read by Matter, updated by controller)
/// - `cmd_tx` - command channel to controller (write by Matter)
pub fn start(
    modem: Modem<'static>,
    sysloop: EspSystemEventLoop,
    nvs: EspDefaultNvsPartition,
    state: SharedState,
    cmd_tx: SyncSender<ControllerCmd>,
) -> Result<std::thread::JoinHandle<()>, Box<dyn std::error::Error>> {
    let handle = std::thread::Builder::new()
        .name("matter".into())
        .stack_size(MATTER_STACK_SIZE)
        .spawn(move || {
            let result = block_on(run_matter(modem, sysloop, nvs, state, cmd_tx));
            if let Err(e) = result {
                log::error!("Matter stack exited with error: {:?}", e);
            }
        })?;

    Ok(handle)
}

/// The async Matter main loop.
async fn run_matter(
    mut modem: Modem<'static>,
    sysloop: EspSystemEventLoop,
    nvs: EspDefaultNvsPartition,
    state: SharedState,
    cmd_tx: SyncSender<ControllerCmd>,
) -> Result<(), esp_idf_matter::matter::error::Error> {
    log::info!("Matter: initializing stack...");

    // Initialize the Matter stack (can only be done once)
    let stack = MATTER_STACK
        .uninit()
        .init_with(EspWifiMatterStack::init_default(
            &DAISPAN_DEV_DET,
            TEST_DEV_COMM,
            &TEST_DEV_ATT,
        ));

    // Initialize async I/O subsystem
    let mounted_event_fs = Arc::new(
        MountedEventfs::mount(3).map_err(|e| {
            log::error!("Failed to mount eventfs: {:?}", e);
            esp_idf_matter::matter::error::ErrorCode::StdIoError
        })?,
    );
    init_async_io(mounted_event_fs.clone()).map_err(|e| {
        log::error!("Failed to init async I/O: {:?}", e);
        esp_idf_matter::matter::error::ErrorCode::StdIoError
    })?;

    // Reduce BT classic memory (we only need BLE)
    reduce_bt_memory(unsafe { modem.reborrow() }).map_err(|e| {
        log::error!("Failed to reduce BT memory: {:?}", e);
        esp_idf_matter::matter::error::ErrorCode::StdIoError
    })?;

    let timers = EspTaskTimerService::new().map_err(|e| {
        log::error!("Failed to create timer service: {:?}", e);
        esp_idf_matter::matter::error::ErrorCode::StdIoError
    })?;

    // Create crypto provider
    let crypto = default_crypto::<NoopRawMutex, _>(rand::thread_rng(), DAC_PRIVKEY);
    let mut weak_rand = crypto.weak_rand()?;

    // --- Create cluster handlers ---

    // Thermostat handler (endpoint 1)
    let thermostat = ThermostatHandler::new(
        state.clone(),
        cmd_tx.clone(),
        Dataver::new_rand(&mut weak_rand),
    );

    // Fan handler on thermostat endpoint (endpoint 1)
    let fan_ep1 = FanHandler::new(
        state.clone(),
        cmd_tx.clone(),
        Dataver::new_rand(&mut weak_rand),
    );

    // Fan handler on dedicated fan endpoint (endpoint 2)
    let fan_ep2 = FanHandler::new(
        state.clone(),
        cmd_tx.clone(),
        Dataver::new_rand(&mut weak_rand),
    );

    // Swing handler (endpoint 3) using built-in OnOff
    let swing_logic = SwingOnOffLogic::new(state.clone(), cmd_tx.clone());
    let swing = OnOffHandler::new_standalone(
        Dataver::new_rand(&mut weak_rand),
        SWING_ENDPOINT_ID,
        swing_logic,
    );

    // --- Chain all handlers ---
    //
    // The handler chain is matched by (endpoint_id, cluster_id).
    // EmptyHandler is the base; each `.chain()` adds a handler for a
    // specific (endpoint, cluster) pair.

    let handler = EmptyHandler
        // Endpoint 1: Thermostat cluster (custom AsyncHandler)
        .chain(
            EpClMatcher::new(Some(THERMOSTAT_ENDPOINT_ID), Some(CLUSTER_THERMOSTAT)),
            &thermostat,
        )
        // Endpoint 1: Fan Control cluster (custom AsyncHandler)
        .chain(
            EpClMatcher::new(Some(THERMOSTAT_ENDPOINT_ID), Some(CLUSTER_FAN_CONTROL)),
            &fan_ep1,
        )
        // Endpoint 1: Descriptor cluster
        .chain(
            EpClMatcher::new(Some(THERMOSTAT_ENDPOINT_ID), Some(DescHandler::CLUSTER.id)),
            Async(desc::DescHandler::new(Dataver::new_rand(&mut weak_rand)).adapt()),
        )
        // Endpoint 2: Fan Control cluster (custom AsyncHandler)
        .chain(
            EpClMatcher::new(Some(FAN_ENDPOINT_ID), Some(CLUSTER_FAN_CONTROL)),
            &fan_ep2,
        )
        // Endpoint 2: Descriptor cluster
        .chain(
            EpClMatcher::new(Some(FAN_ENDPOINT_ID), Some(DescHandler::CLUSTER.id)),
            Async(desc::DescHandler::new(Dataver::new_rand(&mut weak_rand)).adapt()),
        )
        // Endpoint 3: On/Off cluster (swing) - uses built-in OnOffHandler
        .chain(
            EpClMatcher::new(
                Some(SWING_ENDPOINT_ID),
                Some(SwingOnOffLogic::CLUSTER.id),
            ),
            on_off::HandlerAsyncAdaptor(&swing),
        )
        // Endpoint 3: Descriptor cluster
        .chain(
            EpClMatcher::new(Some(SWING_ENDPOINT_ID), Some(DescHandler::CLUSTER.id)),
            Async(desc::DescHandler::new(Dataver::new_rand(&mut weak_rand)).adapt()),
        );

    // Create persister (dummy for now -- TODO: use EspKvBlobStore for NVS persistence)
    let persist = stack
        .create_persist_with_comm_window(
            &crypto,
            esp_idf_matter::stack::persist::DummyKvBlobStore,
        )
        .await?;

    log::info!("Matter: starting stack with WiFi+BLE coexistence...");

    // Run the Matter stack
    let matter = pin!(stack.run_coex(
        EspMatterWifi::new_with_builtin_mdns(modem, sysloop, timers, nvs, stack),
        &persist,
        &crypto,
        (NODE, handler),
        (), // No user future
    ));

    matter.await?;

    Ok(())
}
