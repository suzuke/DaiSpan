//! Custom Matter Fan Control cluster handler.
//!
//! Implements Matter Fan Control cluster (0x0202) as a raw `AsyncHandler`
//! since rs-matter does not yet provide a built-in Fan cluster.
//!
//! Attributes exposed:
//! - FanMode (0x0000) - Off/Low/Medium/High/Auto
//! - PercentSetting (0x0002) - 0-100% fan speed
//! - PercentCurrent (0x0003) - current 0-100% (read-only)
//! - FanModeSequence (0x0001) - OffLowMedHighAuto

use core::cell::Cell;
use std::sync::mpsc::SyncSender;

use crate::controller::state::{ControllerCmd, SharedState};
use crate::s21::types::FanSpeed;

use esp_idf_matter::matter::dm::{
    Access, AsyncHandler, AttrId, Attribute, Cluster, ClusterId, Dataver,
    HandlerContext, InvokeContext, InvokeReply, Quality, ReadContext,
    ReadReply, Reply, WriteContext,
};
use esp_idf_matter::matter::error::{Error, ErrorCode};

/// Matter Fan Control cluster ID (0x0202)
pub const CLUSTER_FAN_CONTROL: ClusterId = 0x0202;

// Fan Control attribute IDs
const ATTR_FAN_MODE: AttrId = 0x0000;
const ATTR_FAN_MODE_SEQUENCE: AttrId = 0x0001;
const ATTR_PERCENT_SETTING: AttrId = 0x0002;
const ATTR_PERCENT_CURRENT: AttrId = 0x0003;
const ATTR_SPEED_MAX: AttrId = 0x0004;
const ATTR_SPEED_SETTING: AttrId = 0x0005;
const ATTR_SPEED_CURRENT: AttrId = 0x0006;

// FanMode enum values (Matter spec)
const FAN_MODE_OFF: u8 = 0;
const FAN_MODE_LOW: u8 = 1;
const FAN_MODE_MEDIUM: u8 = 2;
const FAN_MODE_HIGH: u8 = 3;
const FAN_MODE_AUTO: u8 = 5;

// FanModeSequence: OffLowMedHighAuto = 4
const FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO: u8 = 4;

/// Static cluster definition for the Fan Control cluster.
pub const FAN_CONTROL_CLUSTER: Cluster<'static> = Cluster::new(
    CLUSTER_FAN_CONTROL,
    4, // revision
    0, // feature map
    &[
        Attribute::new(ATTR_FAN_MODE, Access::RWVM, Quality::NONE),
        Attribute::new(ATTR_FAN_MODE_SEQUENCE, Access::RV, Quality::NONE),
        Attribute::new(ATTR_PERCENT_SETTING, Access::RWVM, Quality::X),
        Attribute::new(ATTR_PERCENT_CURRENT, Access::RV, Quality::NONE),
        Attribute::new(ATTR_SPEED_MAX, Access::RV, Quality::F),
        Attribute::new(ATTR_SPEED_SETTING, Access::RWVM, Quality::X),
        Attribute::new(ATTR_SPEED_CURRENT, Access::RV, Quality::NONE),
    ],
    &[],
    |_, _, _| true,
    |_, _, _| true,
);

/// Convert FanSpeed to Matter FanMode.
fn fan_speed_to_mode(speed: FanSpeed) -> u8 {
    match speed {
        FanSpeed::Auto => FAN_MODE_AUTO,
        FanSpeed::Quiet | FanSpeed::Speed1 => FAN_MODE_LOW,
        FanSpeed::Speed2 | FanSpeed::Speed3 => FAN_MODE_MEDIUM,
        FanSpeed::Speed4 | FanSpeed::Speed5 => FAN_MODE_HIGH,
    }
}

/// Convert Matter FanMode to FanSpeed.
fn mode_to_fan_speed(mode: u8) -> FanSpeed {
    match mode {
        FAN_MODE_OFF => FanSpeed::Auto, // No "off fan" in Daikin; use Auto
        FAN_MODE_LOW => FanSpeed::Speed1,
        FAN_MODE_MEDIUM => FanSpeed::Speed3,
        FAN_MODE_HIGH => FanSpeed::Speed5,
        FAN_MODE_AUTO => FanSpeed::Auto,
        _ => FanSpeed::Auto,
    }
}

/// The custom fan control handler.
pub struct FanHandler {
    state: SharedState,
    cmd_tx: SyncSender<ControllerCmd>,
    dataver: Dataver,
    /// Local shadow of fan mode.
    local_fan_mode: Cell<u8>,
    /// Local shadow of percent setting.
    local_percent: Cell<u8>,
}

impl FanHandler {
    pub fn new(
        state: SharedState,
        cmd_tx: SyncSender<ControllerCmd>,
        dataver: Dataver,
    ) -> Self {
        Self {
            state,
            cmd_tx,
            dataver,
            local_fan_mode: Cell::new(FAN_MODE_AUTO),
            local_percent: Cell::new(0),
        }
    }

    fn handle_read(&self, attr_id: AttrId, reply: impl Reply) -> Result<(), Error> {
        let status = self.state.read();

        match attr_id {
            ATTR_FAN_MODE => {
                if status.valid {
                    reply.set(fan_speed_to_mode(status.fan_speed))?;
                } else {
                    reply.set(self.local_fan_mode.get())?;
                }
            }
            ATTR_FAN_MODE_SEQUENCE => {
                reply.set(FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO)?;
            }
            ATTR_PERCENT_SETTING => {
                if status.valid {
                    reply.set(status.fan_speed.to_percent())?;
                } else {
                    reply.set(self.local_percent.get())?;
                }
            }
            ATTR_PERCENT_CURRENT => {
                if status.valid {
                    reply.set(status.fan_speed.to_percent())?;
                } else {
                    reply.set(0u8)?;
                }
            }
            ATTR_SPEED_MAX => {
                reply.set(5u8)?; // 5 speeds
            }
            ATTR_SPEED_SETTING => {
                if status.valid {
                    // Map FanSpeed enum to 0-5 speed value
                    let speed: u8 = match status.fan_speed {
                        FanSpeed::Auto => 0,
                        FanSpeed::Quiet => 1,
                        FanSpeed::Speed1 => 1,
                        FanSpeed::Speed2 => 2,
                        FanSpeed::Speed3 => 3,
                        FanSpeed::Speed4 => 4,
                        FanSpeed::Speed5 => 5,
                    };
                    reply.set(speed)?;
                } else {
                    reply.set(0u8)?;
                }
            }
            ATTR_SPEED_CURRENT => {
                if status.valid {
                    let speed: u8 = match status.fan_speed {
                        FanSpeed::Auto => 0,
                        FanSpeed::Quiet => 1,
                        FanSpeed::Speed1 => 1,
                        FanSpeed::Speed2 => 2,
                        FanSpeed::Speed3 => 3,
                        FanSpeed::Speed4 => 4,
                        FanSpeed::Speed5 => 5,
                    };
                    reply.set(speed)?;
                } else {
                    reply.set(0u8)?;
                }
            }
            _ => {
                return Err(ErrorCode::AttributeNotFound.into());
            }
        }

        Ok(())
    }

    fn handle_write(&self, ctx: &impl WriteContext) -> Result<(), Error> {
        let attr_id = ctx.attr().attr_id;
        let data = ctx.data();

        match attr_id {
            ATTR_FAN_MODE => {
                let mode: u8 = data.u8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
                if mode > 5 {
                    return Err(ErrorCode::ConstraintError.into());
                }
                self.local_fan_mode.set(mode);

                let fan_speed = mode_to_fan_speed(mode);
                let _ = self.cmd_tx.try_send(ControllerCmd::SetFan(fan_speed));

                self.dataver.changed();
                ctx.notify_changed();
            }
            ATTR_PERCENT_SETTING => {
                let pct: u8 = data.u8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
                if pct > 100 {
                    return Err(ErrorCode::ConstraintError.into());
                }
                self.local_percent.set(pct);

                let fan_speed = FanSpeed::from_percent(pct);
                let _ = self.cmd_tx.try_send(ControllerCmd::SetFan(fan_speed));

                self.dataver.changed();
                ctx.notify_changed();
            }
            ATTR_SPEED_SETTING => {
                let speed: u8 = data.u8().map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
                if speed > 5 {
                    return Err(ErrorCode::ConstraintError.into());
                }

                let fan_speed = match speed {
                    0 => FanSpeed::Auto,
                    1 => FanSpeed::Speed1,
                    2 => FanSpeed::Speed2,
                    3 => FanSpeed::Speed3,
                    4 => FanSpeed::Speed4,
                    5 => FanSpeed::Speed5,
                    _ => FanSpeed::Auto,
                };
                let _ = self.cmd_tx.try_send(ControllerCmd::SetFan(fan_speed));

                self.dataver.changed();
                ctx.notify_changed();
            }
            _ => {
                return Err(ErrorCode::UnsupportedWrite.into());
            }
        }

        Ok(())
    }
}

impl AsyncHandler for FanHandler {
    fn read_awaits(&self, _ctx: impl ReadContext) -> bool {
        false
    }

    fn write_awaits(&self, _ctx: impl WriteContext) -> bool {
        false
    }

    fn invoke_awaits(&self, _ctx: impl InvokeContext) -> bool {
        false
    }

    async fn read(&self, ctx: impl ReadContext, reply: impl ReadReply) -> Result<(), Error> {
        let attr = ctx.attr();
        if attr.cluster_id != CLUSTER_FAN_CONTROL {
            return Err(ErrorCode::ClusterNotFound.into());
        }

        if let Some(writer) = reply.with_dataver(self.dataver.get())? {
            self.handle_read(attr.attr_id, writer)?;
        }

        Ok(())
    }

    async fn write(&self, ctx: impl WriteContext) -> Result<(), Error> {
        let attr = ctx.attr();
        if attr.cluster_id != CLUSTER_FAN_CONTROL {
            return Err(ErrorCode::ClusterNotFound.into());
        }

        self.handle_write(&ctx)
    }

    async fn invoke(
        &self,
        _ctx: impl InvokeContext,
        _reply: impl InvokeReply,
    ) -> Result<(), Error> {
        Err(ErrorCode::CommandNotFound.into())
    }
}
