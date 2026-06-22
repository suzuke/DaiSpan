//! Custom Matter Thermostat cluster handler.
//!
//! Implements Matter Thermostat cluster (0x0201) as a raw `AsyncHandler`
//! since rs-matter does not yet provide a built-in Thermostat cluster.
//!
//! Attributes exposed:
//! - LocalTemperature (0x0000) - read from SharedState
//! - OccupiedCoolingSetpoint (0x0011) - target temp when cooling
//! - OccupiedHeatingSetpoint (0x0012) - target temp when heating
//! - SystemMode (0x001C) - Off/Auto/Cool/Heat
//! - ControlSequenceOfOperation (0x001B) - reports CoolingAndHeating
//!
//! Temperature values are in 0.01 degC units per Matter spec.

use core::cell::Cell;
use std::sync::mpsc::SyncSender;

use crate::controller::state::{ControllerCmd, MatterMode, SharedState};
use esp_idf_matter::matter::dm::{
    Access, AsyncHandler, AttrId, Attribute, Cluster, ClusterId, Command, Dataver,
    InvokeContext, InvokeReply, Quality, ReadContext,
    ReadReply, Reply, WriteContext,
};
use esp_idf_matter::matter::error::{Error, ErrorCode};

// SetpointRaiseLower command ID (mandatory per Matter spec)
const CMD_SETPOINT_RAISE_LOWER: u32 = 0x00;

/// Matter Thermostat cluster ID (0x0201)
pub const CLUSTER_THERMOSTAT: ClusterId = 0x0201;

// Thermostat attribute IDs (from Matter spec)
const ATTR_LOCAL_TEMPERATURE: AttrId = 0x0000;
const ATTR_OCCUPIED_COOLING_SETPOINT: AttrId = 0x0011;
const ATTR_OCCUPIED_HEATING_SETPOINT: AttrId = 0x0012;
const ATTR_SYSTEM_MODE: AttrId = 0x001C;
const ATTR_CONTROL_SEQUENCE: AttrId = 0x001B;
const ATTR_MIN_HEAT_SETPOINT_LIMIT: AttrId = 0x0015;
const ATTR_MAX_HEAT_SETPOINT_LIMIT: AttrId = 0x0016;
const ATTR_MIN_COOL_SETPOINT_LIMIT: AttrId = 0x0017;
const ATTR_MAX_COOL_SETPOINT_LIMIT: AttrId = 0x0018;
// Absolute setpoint limits: Apple Home uses these to bound the slider; with only
// the Min/Max limits present (no Abs limits) the slider can clamp to null/NaN.
const ATTR_ABS_MIN_HEAT_SETPOINT_LIMIT: AttrId = 0x0003;
const ATTR_ABS_MAX_HEAT_SETPOINT_LIMIT: AttrId = 0x0004;
const ATTR_ABS_MIN_COOL_SETPOINT_LIMIT: AttrId = 0x0005;
const ATTR_ABS_MAX_COOL_SETPOINT_LIMIT: AttrId = 0x0006;
// Running mode/state (present in the known-working reference implementation).
const ATTR_THERMOSTAT_RUNNING_MODE: AttrId = 0x001E;
const ATTR_THERMOSTAT_RUNNING_STATE: AttrId = 0x0029;
const ATTR_MIN_SETPOINT_DEAD_BAND: AttrId = 0x0019;

// SystemMode values
const SYSTEM_MODE_OFF: u8 = 0;
const SYSTEM_MODE_AUTO: u8 = 1;
const SYSTEM_MODE_COOL: u8 = 3;
const SYSTEM_MODE_HEAT: u8 = 4;

// ControlSequenceOfOperation: CoolingAndHeating = 4
const CONTROL_SEQ_COOLING_AND_HEATING: u8 = 4;

// Temperature range in 0.01 degC (Daikin: 18..32)
const MIN_TEMP: i16 = 1800;  // 18.0 C
const MAX_TEMP: i16 = 3200;  // 32.0 C

/// Static cluster definition for the Thermostat cluster.
///
/// Minimal attributes needed for a basic Thermostat device.
pub const THERMOSTAT_CLUSTER: Cluster<'static> = Cluster::new(
    CLUSTER_THERMOSTAT,
    7, // revision (aligned with connectedhomeip thermostat.matter / working reference)
    // DIAGNOSTIC: exact copy of the Apple-confirmed-rendering heat-only thermostat from
    // home-assistant-matter-hub #497. feature 0x01 (Heating only), minimal attribute set,
    // ControlSeq=HeatingOnly(2), SystemMode=Heat(4). If THIS renders in Apple Home then
    // rs-matter CAN render a thermostat and our cool/auto combo was the trigger; if not,
    // rs-matter's raw thermostat can't render in Apple at all. Cooling/RunningMode removed.
    0x01,
    &[
        Attribute::new(ATTR_LOCAL_TEMPERATURE, Access::RV, Quality::N),
        Attribute::new(ATTR_OCCUPIED_HEATING_SETPOINT, Access::RA, Quality::N),
        Attribute::new(ATTR_SYSTEM_MODE, Access::RA, Quality::N),
        Attribute::new(ATTR_CONTROL_SEQUENCE, Access::RA, Quality::N),
        Attribute::new(ATTR_THERMOSTAT_RUNNING_STATE, Access::RV, Quality::N),
        Attribute::new(ATTR_ABS_MIN_HEAT_SETPOINT_LIMIT, Access::RV, Quality::N),
        Attribute::new(ATTR_ABS_MAX_HEAT_SETPOINT_LIMIT, Access::RV, Quality::N),
        Attribute::new(ATTR_MIN_HEAT_SETPOINT_LIMIT, Access::RA, Quality::N),
        Attribute::new(ATTR_MAX_HEAT_SETPOINT_LIMIT, Access::RA, Quality::N),
    ],
    &[Command::new(CMD_SETPOINT_RAISE_LOWER, None, Access::WO)],
    |_, _, _| true,
    |_, _, _| true,
);

/// The custom thermostat handler. Reads from SharedState, sends commands via cmd_tx.
pub struct ThermostatHandler {
    state: SharedState,
    cmd_tx: SyncSender<ControllerCmd>,
    dataver: Dataver,
    /// Local shadow of system mode for write-before-read scenarios.
    local_system_mode: Cell<u8>,
    /// Local shadow of setpoints (in 0.01 degC).
    local_cooling_setpoint: Cell<i16>,
    local_heating_setpoint: Cell<i16>,
}

impl ThermostatHandler {
    pub fn new(
        state: SharedState,
        cmd_tx: SyncSender<ControllerCmd>,
        dataver: Dataver,
    ) -> Self {
        Self {
            state,
            cmd_tx,
            dataver,
            local_system_mode: Cell::new(SYSTEM_MODE_HEAT),
            local_cooling_setpoint: Cell::new(2400), // 24.0C (matches working reference)
            local_heating_setpoint: Cell::new(2200), // 22.0C (matches working reference)
        }
    }

    /// Convert float celsius to Matter 0.01 degC units.
    fn to_matter_temp(celsius: f32) -> i16 {
        (celsius * 100.0) as i16
    }

    /// Convert Matter 0.01 degC units to float celsius.
    fn from_matter_temp(matter: i16) -> f32 {
        matter as f32 / 100.0
    }

    /// Clamp temperature to Daikin range.
    fn clamp_temp(matter: i16) -> i16 {
        matter.clamp(MIN_TEMP, MAX_TEMP)
    }

    /// Get the current system mode from SharedState.
    fn current_system_mode(&self) -> u8 {
        let status = self.state.read();
        if !status.power {
            SYSTEM_MODE_OFF
        } else {
            match MatterMode::from_ac_mode(status.mode) {
                MatterMode::Off => SYSTEM_MODE_OFF,
                // heat-only diagnostic (feature 0x01) → everything non-off maps to Heat.
                MatterMode::Auto => SYSTEM_MODE_HEAT,
                MatterMode::Cool => SYSTEM_MODE_HEAT,
                MatterMode::Heat => SYSTEM_MODE_HEAT,
            }
        }
    }

    /// Handle an attribute read.
    fn handle_read(&self, attr_id: AttrId, reply: impl Reply) -> Result<(), Error> {
        let status = self.state.read();

        match attr_id {
            ATTR_LOCAL_TEMPERATURE => {
                // Matter uses Nullable<i16> in 0.01 degC. Apple Home renders a
                // blank / NaN gauge for a null LocalTemperature, so fall back to a
                // sane default (20.0C) when there is no live reading yet (no AC
                // connected / S21 poll failing) instead of reporting null. This
                // matches the working reference, whose LocalTemperature is never null.
                if status.valid {
                    reply.set(Self::to_matter_temp(status.current_temp))?;
                } else {
                    reply.set(2000i16)?; // 20.00 C placeholder until real data arrives
                }
            }
            ATTR_OCCUPIED_COOLING_SETPOINT => {
                if status.valid {
                    reply.set(Self::to_matter_temp(status.target_temp))?;
                } else {
                    reply.set(self.local_cooling_setpoint.get())?;
                }
            }
            ATTR_OCCUPIED_HEATING_SETPOINT => {
                if status.valid {
                    reply.set(Self::to_matter_temp(status.target_temp))?;
                } else {
                    reply.set(self.local_heating_setpoint.get())?;
                }
            }
            ATTR_SYSTEM_MODE => {
                if status.valid {
                    reply.set(self.current_system_mode())?;
                } else {
                    reply.set(self.local_system_mode.get())?;
                }
            }
            ATTR_CONTROL_SEQUENCE => {
                reply.set(2u8)?; // HeatingOnly (matches #497 heat-only diagnostic config)
            }
            ATTR_THERMOSTAT_RUNNING_MODE => {
                // Actual running mode derived from power + room vs setpoint.
                // Enum: 0=Off, 3=Cool, 4=Heat.
                let mode: u8 = if !status.valid || !status.power {
                    0
                } else if status.current_temp < status.target_temp {
                    SYSTEM_MODE_HEAT
                } else {
                    SYSTEM_MODE_COOL
                };
                reply.set(mode)?;
            }
            ATTR_THERMOSTAT_RUNNING_STATE => {
                // RelayState bitmap16: bit0 = Heat on, bit1 = Cool on.
                let running_state: u16 = if !status.valid || !status.power {
                    0
                } else if status.current_temp < status.target_temp {
                    0x0001
                } else {
                    0x0002
                };
                reply.set(running_state)?;
            }
            ATTR_MIN_HEAT_SETPOINT_LIMIT | ATTR_MIN_COOL_SETPOINT_LIMIT
            | ATTR_ABS_MIN_HEAT_SETPOINT_LIMIT | ATTR_ABS_MIN_COOL_SETPOINT_LIMIT => {
                reply.set(MIN_TEMP)?;
            }
            ATTR_MAX_HEAT_SETPOINT_LIMIT | ATTR_MAX_COOL_SETPOINT_LIMIT
            | ATTR_ABS_MAX_HEAT_SETPOINT_LIMIT | ATTR_ABS_MAX_COOL_SETPOINT_LIMIT => {
                reply.set(MAX_TEMP)?;
            }
            _ => {
                return Err(ErrorCode::AttributeNotFound.into());
            }
        }

        Ok(())
    }

    /// Handle an attribute write.
    fn handle_write(&self, ctx: &impl WriteContext) -> Result<(), Error> {
        let attr_id = ctx.attr().attr_id;
        let data = ctx.data();

        match attr_id {
            ATTR_OCCUPIED_COOLING_SETPOINT | ATTR_OCCUPIED_HEATING_SETPOINT => {
                let raw: i16 = data.i16().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
                let clamped = Self::clamp_temp(raw);
                let temp = Self::from_matter_temp(clamped);

                if attr_id == ATTR_OCCUPIED_COOLING_SETPOINT {
                    self.local_cooling_setpoint.set(clamped);
                } else {
                    self.local_heating_setpoint.set(clamped);
                }

                // Send to controller
                if let Err(e) = self.cmd_tx.try_send(ControllerCmd::SetTemp(temp)) {
                    log::warn!("Failed to send SetTemp: {}", e);
                }

                self.dataver.changed();
                ctx.notify_changed();
            }
            ATTR_SYSTEM_MODE => {
                let mode: u8 = data.u8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
                self.local_system_mode.set(mode);

                let matter_mode = match mode {
                    SYSTEM_MODE_OFF => MatterMode::Off,
                    SYSTEM_MODE_AUTO => MatterMode::Auto,
                    SYSTEM_MODE_COOL => MatterMode::Cool,
                    SYSTEM_MODE_HEAT => MatterMode::Heat,
                    _ => return Err(ErrorCode::ConstraintError.into()),
                };

                if let Err(e) = self.cmd_tx.try_send(ControllerCmd::SetMode(matter_mode)) {
                    log::warn!("Failed to send SetMode: {}", e);
                }

                self.dataver.changed();
                ctx.notify_changed();
            }
            _ => {
                return Err(ErrorCode::UnsupportedAccess.into());
            }
        }

        Ok(())
    }
}

impl AsyncHandler for ThermostatHandler {
    fn read_awaits(&self, _ctx: impl ReadContext) -> bool {
        false // We never await on reads
    }

    fn write_awaits(&self, _ctx: impl WriteContext) -> bool {
        false
    }

    fn invoke_awaits(&self, _ctx: impl InvokeContext) -> bool {
        false
    }

    async fn read(&self, ctx: impl ReadContext, reply: impl ReadReply) -> Result<(), Error> {
        let attr = ctx.attr();
        if attr.cluster_id != CLUSTER_THERMOSTAT {
            return Err(ErrorCode::ClusterNotFound.into());
        }

        log::info!("Thermostat read: ep={} attr=0x{:04X}", attr.endpoint_id, attr.attr_id);

        if let Some(writer) = reply.with_dataver(self.dataver.get())? {
            if let Err(e) = self.handle_read(attr.attr_id, writer) {
                log::warn!("Thermostat read attr 0x{:04X} error: {:?}", attr.attr_id, e);
                return Err(e);
            }
        }

        Ok(())
    }

    async fn write(&self, ctx: impl WriteContext) -> Result<(), Error> {
        let attr = ctx.attr();
        if attr.cluster_id != CLUSTER_THERMOSTAT {
            return Err(ErrorCode::ClusterNotFound.into());
        }

        self.handle_write(&ctx)
    }

    async fn invoke(
        &self,
        ctx: impl InvokeContext,
        _reply: impl InvokeReply,
    ) -> Result<(), Error> {
        let cmd = ctx.cmd();
        if cmd.cmd_id != CMD_SETPOINT_RAISE_LOWER {
            return Err(ErrorCode::CommandNotFound.into());
        }

        // SetpointRaiseLower: Mode (ctx 0, enum8), Amount (ctx 1, int8 in 0.1°C steps)
        let data = ctx.data();
        let fields = data.structure().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;

        let mode: u8 = fields.find_ctx(0)?.u8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
        let amount: i8 = fields.find_ctx(1)?.i8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;

        // Amount is in 0.1°C steps, convert to 0.01°C (Matter units)
        let delta: i16 = (amount as i16) * 10;

        // Apply delta based on mode: 0=Heat, 1=Cool, 2=Both
        match mode {
            0 => {
                let new_sp = Self::clamp_temp(self.local_heating_setpoint.get() + delta);
                self.local_heating_setpoint.set(new_sp);
                let _ = self.cmd_tx.try_send(ControllerCmd::SetTemp(Self::from_matter_temp(new_sp)));
            }
            1 => {
                let new_sp = Self::clamp_temp(self.local_cooling_setpoint.get() + delta);
                self.local_cooling_setpoint.set(new_sp);
                let _ = self.cmd_tx.try_send(ControllerCmd::SetTemp(Self::from_matter_temp(new_sp)));
            }
            2 => {
                let new_heat = Self::clamp_temp(self.local_heating_setpoint.get() + delta);
                let new_cool = Self::clamp_temp(self.local_cooling_setpoint.get() + delta);
                self.local_heating_setpoint.set(new_heat);
                self.local_cooling_setpoint.set(new_cool);
                let _ = self.cmd_tx.try_send(ControllerCmd::SetTemp(Self::from_matter_temp(new_cool)));
            }
            _ => return Err(ErrorCode::ConstraintError.into()),
        }

        self.dataver.changed();
        Ok(())
    }
}
