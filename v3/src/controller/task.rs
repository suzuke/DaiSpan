//! Controller task: owns S21 UART, polls status, processes commands.
//!
//! Ported from v2/main/controller/thermostat_ctrl.c (475 lines).

use std::sync::mpsc::{self, Receiver, SyncSender};
use std::time::{Duration, Instant};

use crate::s21::types::*;
use crate::s21::uart::S21Uart;
use crate::s21::version::{S21Features, S21Version};

use super::state::{ControllerCmd, MatterMode, SharedState};

// Timing constants
const POLL_INTERVAL: Duration = Duration::from_millis(6000);
const ERROR_RECOVERY: Duration = Duration::from_millis(30000);
const MODE_PROTECT: Duration = Duration::from_millis(30000);
const FAN_PROTECT: Duration = Duration::from_millis(10000);
const CMD_RECV_TIMEOUT: Duration = Duration::from_millis(100);
const MAX_ERRORS: u32 = 10;
const CMD_QUEUE_SIZE: usize = 8;

/// Controller that owns the S21 UART and manages AC state.
pub struct Controller<'d> {
    uart: S21Uart<'d>,
    rx: Receiver<ControllerCmd>,
    shared: SharedState,

    // Local state
    local: AcStatus,

    // Detected protocol info
    version: S21Version,
    #[allow(dead_code)]
    features: S21Features,

    // Dirty flags for pending commands
    dirty_power: bool,
    dirty_mode: bool,
    dirty_temp: bool,
    dirty_fan: bool,
    dirty_swing: bool,

    // Error tracking
    consecutive_errors: u32,
    last_success: Instant,

    // Mode/fan protection
    last_mode_set: Option<Instant>,
    last_user_mode: AcMode,
    last_fan_set: Option<Instant>,
    last_user_fan: FanSpeed,

    // Preserve target Matter mode across lossy AC->Matter conversions
    target_matter_mode: MatterMode,
}

impl<'d> Controller<'d> {
    /// Create a new controller. Returns the controller and a command sender.
    ///
    /// The `SyncSender` has a bounded buffer of `CMD_QUEUE_SIZE` entries.
    /// Callers (HomeKit) should use `try_send()` to avoid blocking.
    pub fn new(uart: S21Uart<'d>, shared: SharedState) -> (Self, SyncSender<ControllerCmd>) {
        let (tx, rx) = mpsc::sync_channel(CMD_QUEUE_SIZE);

        let ctrl = Self {
            uart,
            rx,
            shared,
            local: AcStatus::default(),
            version: S21Version::V1,
            features: S21Features::default(),
            dirty_power: false,
            dirty_mode: false,
            dirty_temp: false,
            dirty_fan: false,
            dirty_swing: false,
            consecutive_errors: 0,
            last_success: Instant::now(),
            last_mode_set: None,
            last_user_mode: AcMode::Auto,
            last_fan_set: None,
            last_user_fan: FanSpeed::Auto,
            target_matter_mode: MatterMode::Auto,
        };

        (ctrl, tx)
    }

    /// Initialize: detect S21 version and features.
    /// Call once before `run()`.
    pub fn init(&mut self) {
        log::info!("Controller initializing...");

        // Let UART settle
        std::thread::sleep(Duration::from_millis(1000));

        self.version = self.uart.detect_version();
        self.features = self.uart.detect_features(self.version);

        log::info!(
            "Controller initialized: S21 {}",
            self.version
        );
    }

    /// Main loop: process commands, poll status, handle recovery.
    /// This method never returns.
    pub fn run(&mut self) -> ! {
        log::info!("Controller task running");

        // Initial status poll
        self.poll_status();
        let mut last_poll = Instant::now();

        loop {
            // Drain command queue (non-blocking with 100ms timeout)
            while let Ok(cmd) = self.rx.recv_timeout(CMD_RECV_TIMEOUT) {
                self.process_command(cmd);
            }

            let now = Instant::now();

            if self.is_recovering() {
                // In recovery mode: attempt recovery after interval
                if now.duration_since(self.last_success) > ERROR_RECOVERY {
                    log::info!("Attempting recovery...");
                    self.last_success = now; // Reset timer regardless of outcome
                    self.consecutive_errors = MAX_ERRORS - 1; // One chance
                    self.sync_dirty();
                    if !self.is_recovering() {
                        self.poll_status();
                        last_poll = Instant::now();
                    }
                }
            } else if now.duration_since(last_poll) >= POLL_INTERVAL {
                // Normal polling: sync dirty state first, then poll
                self.sync_dirty();
                if !self.is_recovering() {
                    self.poll_status();
                }
                last_poll = Instant::now();
            }
        }
    }

    // --- Command processing ---

    fn process_command(&mut self, cmd: ControllerCmd) {
        match cmd {
            ControllerCmd::SetPower(on) => {
                log::info!("Cmd: power={}", on);
                self.local.power = on;
                self.dirty_power = true;
                if !self.is_recovering() {
                    self.try_send_d1();
                }
            }

            ControllerCmd::SetMode(matter_mode) => {
                log::info!("Cmd: mode={:?}", matter_mode);

                if matter_mode == MatterMode::Off {
                    self.local.power = false;
                    self.dirty_power = true;
                } else {
                    if let Some(ac_mode) = matter_mode.to_ac_mode() {
                        self.local.mode = ac_mode;
                        self.target_matter_mode = matter_mode;
                        self.last_mode_set = Some(Instant::now());
                        self.last_user_mode = ac_mode;
                        self.dirty_mode = true;
                    }
                    if !self.local.power {
                        self.local.power = true;
                        self.dirty_power = true;
                    }
                }

                if !self.is_recovering() {
                    self.try_send_d1();
                }
            }

            ControllerCmd::SetTemp(temp) => {
                log::info!("Cmd: temp={:.1}", temp);
                self.local.target_temp = temp;
                self.dirty_temp = true;
                if !self.is_recovering() {
                    self.try_send_d1();
                }
            }

            ControllerCmd::SetFan(fan) => {
                log::info!("Cmd: fan={:?}", fan);
                self.local.fan_speed = fan;
                self.last_fan_set = Some(Instant::now());
                self.last_user_fan = fan;
                self.dirty_fan = true;
                if !self.is_recovering() {
                    self.try_send_d1();
                }
            }

            ControllerCmd::SetSwing(axis, enabled) => {
                log::info!("Cmd: swing {:?}={}", axis, enabled);
                match axis {
                    SwingAxis::Vertical => self.local.swing_vertical = enabled,
                    SwingAxis::Horizontal => self.local.swing_horizontal = enabled,
                }
                self.dirty_swing = true;
                if !self.is_recovering() {
                    if self
                        .uart
                        .set_swing(self.local.swing_vertical, self.local.swing_horizontal)
                        .is_ok()
                    {
                        self.dirty_swing = false;
                        self.reset_errors();
                    } else {
                        self.handle_error("set_swing");
                    }
                }
            }
        }

        self.publish();
    }

    // --- Polling ---

    fn poll_status(&mut self) {
        let now = Instant::now();
        let mut any_success = false;

        // Query device status (F1 -> G1)
        match self.uart.query_status() {
            Ok((power, mode, temp, fan)) => {
                any_success = true;
                self.local.power = power;

                // Mode protection: keep user-set mode until AC settles
                if let Some(mode_set_time) = self.last_mode_set {
                    if now.duration_since(mode_set_time) < MODE_PROTECT {
                        self.local.mode = self.last_user_mode;
                        log::debug!("Mode protected");
                    } else {
                        self.local.mode = mode;
                        self.update_matter_mode_from_ac(mode);
                    }
                } else {
                    self.local.mode = mode;
                    self.update_matter_mode_from_ac(mode);
                }

                self.local.target_temp = temp;

                // Fan speed protection
                if let Some(fan_set_time) = self.last_fan_set {
                    if now.duration_since(fan_set_time) < FAN_PROTECT {
                        self.local.fan_speed = self.last_user_fan;
                    } else {
                        self.local.fan_speed = fan;
                    }
                } else {
                    self.local.fan_speed = fan;
                }
            }
            Err(_) => {
                self.handle_error("poll_status");
            }
        }

        // Query temperature (RH -> SH), only if status succeeded
        if any_success {
            match self.uart.query_temperature() {
                Ok(temp) => {
                    self.local.current_temp = temp;
                }
                Err(_) => {
                    self.handle_error("poll_temp");
                    any_success = false;
                }
            }
        }

        // Query swing (F5 -> G5), only if status ok
        if any_success {
            if let Ok((sv, sh)) = self.uart.query_swing() {
                self.local.swing_vertical = sv;
                self.local.swing_horizontal = sh;
            }
            // Don't count swing query failure as error - some ACs don't support F5
        }

        if any_success {
            self.local.valid = true;
            self.reset_errors();
        }

        self.publish();
    }

    /// Update target_matter_mode from AC mode (only for lossless conversions).
    fn update_matter_mode_from_ac(&mut self, mode: AcMode) {
        match mode {
            AcMode::Heat => self.target_matter_mode = MatterMode::Heat,
            AcMode::Cool => self.target_matter_mode = MatterMode::Cool,
            AcMode::Auto | AcMode::Auto2 => self.target_matter_mode = MatterMode::Auto,
            // Dry/Fan: preserve target_matter_mode (lossy)
            AcMode::Dry | AcMode::Fan => {}
        }
    }

    // --- Dirty state sync ---

    fn sync_dirty(&mut self) {
        if !self.dirty_power
            && !self.dirty_mode
            && !self.dirty_temp
            && !self.dirty_fan
            && !self.dirty_swing
        {
            return;
        }

        log::info!(
            "Syncing dirty state (P:{} M:{} T:{} F:{} S:{})",
            self.dirty_power,
            self.dirty_mode,
            self.dirty_temp,
            self.dirty_fan,
            self.dirty_swing
        );

        // D1 command covers power, mode, temp, and fan
        if self.dirty_power || self.dirty_mode || self.dirty_fan {
            if self
                .uart
                .set_state(
                    self.local.power,
                    self.local.mode,
                    self.local.target_temp,
                    self.local.fan_speed,
                )
                .is_ok()
            {
                self.dirty_power = false;
                self.dirty_fan = false;
                self.dirty_temp = false; // set_state includes temp
                if self.dirty_mode {
                    self.last_mode_set = Some(Instant::now());
                    self.last_user_mode = self.local.mode;
                    self.dirty_mode = false;
                }
                self.reset_errors();
            } else {
                self.handle_error("sync_dirty");
                return;
            }
        }

        if self.dirty_temp {
            if self
                .uart
                .set_state(
                    self.local.power,
                    self.local.mode,
                    self.local.target_temp,
                    self.local.fan_speed,
                )
                .is_ok()
            {
                self.dirty_temp = false;
                self.reset_errors();
            } else {
                self.handle_error("sync_temp");
                return;
            }
        }

        if self.dirty_swing {
            if self
                .uart
                .set_swing(self.local.swing_vertical, self.local.swing_horizontal)
                .is_ok()
            {
                self.dirty_swing = false;
                self.reset_errors();
            } else {
                self.handle_error("sync_swing");
            }
        }
    }

    /// Try to immediately send a D1 command; mark dirty on failure.
    fn try_send_d1(&mut self) {
        if self
            .uart
            .set_state(
                self.local.power,
                self.local.mode,
                self.local.target_temp,
                self.local.fan_speed,
            )
            .is_ok()
        {
            self.dirty_power = false;
            self.dirty_mode = false;
            self.dirty_temp = false;
            self.dirty_fan = false;
            self.reset_errors();
        } else {
            self.handle_error("send_d1");
        }
    }

    // --- Error tracking ---

    fn is_recovering(&self) -> bool {
        self.consecutive_errors >= MAX_ERRORS
    }

    fn handle_error(&mut self, op: &str) {
        self.consecutive_errors += 1;
        log::warn!("{} failed (errors: {})", op, self.consecutive_errors);
        if self.consecutive_errors >= MAX_ERRORS {
            log::error!("Entering error recovery mode");
        }
    }

    fn reset_errors(&mut self) {
        if self.consecutive_errors > 0 {
            log::info!("Errors cleared (was: {})", self.consecutive_errors);
            self.consecutive_errors = 0;
        }
        self.last_success = Instant::now();
    }

    // --- State publishing ---

    fn publish(&self) {
        self.shared.update(&self.local);
    }

    // --- Public accessors ---

    /// Get the current Matter mode (accounting for power state).
    #[allow(dead_code)]
    pub fn matter_mode(&self) -> MatterMode {
        if self.local.power {
            self.target_matter_mode
        } else {
            MatterMode::Off
        }
    }

    /// Check if the controller is healthy (not in recovery).
    #[allow(dead_code)]
    pub fn is_healthy(&self) -> bool {
        !self.is_recovering()
    }

    /// Get the current consecutive error count.
    #[allow(dead_code)]
    pub fn error_count(&self) -> u32 {
        self.consecutive_errors
    }
}
