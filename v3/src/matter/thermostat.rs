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
    Access, AsyncHandler, AttrId, Attribute, Cluster, ClusterId, Dataver,
    InvokeContext, InvokeReply, Quality, ReadContext,
    ReadReply, Reply, WriteContext,
};
use esp_idf_matter::matter::error::{Error, ErrorCode};

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
    5, // revision
    0x03, // feature map: Heating (0x01) | Cooling (0x02)
    &[
        Attribute::new(ATTR_LOCAL_TEMPERATURE, Access::RV, Quality::X),
        Attribute::new(ATTR_OCCUPIED_COOLING_SETPOINT, Access::RWVM, Quality::SCENE),
        Attribute::new(ATTR_OCCUPIED_HEATING_SETPOINT, Access::RWVM, Quality::SCENE),
        Attribute::new(ATTR_SYSTEM_MODE, Access::RWVM, Quality::SCENE),
        Attribute::new(ATTR_CONTROL_SEQUENCE, Access::RV, Quality::NONE),
        Attribute::new(ATTR_MIN_HEAT_SETPOINT_LIMIT, Access::RV, Quality::NONE),
        Attribute::new(ATTR_MAX_HEAT_SETPOINT_LIMIT, Access::RV, Quality::NONE),
        Attribute::new(ATTR_MIN_COOL_SETPOINT_LIMIT, Access::RV, Quality::NONE),
        Attribute::new(ATTR_MAX_COOL_SETPOINT_LIMIT, Access::RV, Quality::NONE),
    ],
    &[], // No commands for thermostat (setpoint changes via attribute writes)
    |_, _, _| true,  // all attributes active
    |_, _, _| true,  // all commands active
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
            local_system_mode: Cell::new(SYSTEM_MODE_AUTO),
            local_cooling_setpoint: Cell::new(2100), // 21.0C default
            local_heating_setpoint: Cell::new(2100),
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
                MatterMode::Auto => SYSTEM_MODE_AUTO,
                MatterMode::Cool => SYSTEM_MODE_COOL,
                MatterMode::Heat => SYSTEM_MODE_HEAT,
            }
        }
    }

    /// Handle an attribute read.
    fn handle_read(&self, attr_id: AttrId, reply: impl Reply) -> Result<(), Error> {
        let status = self.state.read();

        match attr_id {
            ATTR_LOCAL_TEMPERATURE => {
                // Matter uses Nullable<i16> in 0.01 degC
                if status.valid {
                    reply.set(Self::to_matter_temp(status.current_temp))?;
                } else {
                    // Null = sensor not available
                    reply.set(esp_idf_matter::matter::tlv::Nullable::<i16>::none())?;
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
                reply.set(CONTROL_SEQ_COOLING_AND_HEATING)?;
            }
            ATTR_MIN_HEAT_SETPOINT_LIMIT | ATTR_MIN_COOL_SETPOINT_LIMIT => {
                reply.set(MIN_TEMP)?;
            }
            ATTR_MAX_HEAT_SETPOINT_LIMIT | ATTR_MAX_COOL_SETPOINT_LIMIT => {
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

        if let Some(writer) = reply.with_dataver(self.dataver.get())? {
            self.handle_read(attr.attr_id, writer)?;
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
        _ctx: impl InvokeContext,
        _reply: impl InvokeReply,
    ) -> Result<(), Error> {
        // No commands defined for our thermostat cluster
        Err(ErrorCode::CommandNotFound.into())
    }
}
