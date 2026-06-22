//! Minimal Matter Identify cluster (0x0003) handler.
//!
//! The Thermostat device type (0x0301) lists Identify as a MANDATORY server
//! cluster. rs-matter has no built-in Identify cluster, so we provide a minimal
//! raw `AsyncHandler`: the IdentifyTime + IdentifyType attributes and a no-op
//! Identify command. Apple Home appears to refuse to render a thermostat whose
//! endpoint is missing this mandatory cluster ("不支援此配件").

use core::cell::Cell;

use esp_idf_matter::matter::dm::{
    Access, AsyncHandler, AttrId, Attribute, Cluster, ClusterId, Command, Dataver,
    InvokeContext, InvokeReply, Quality, ReadContext, ReadReply, Reply, WriteContext,
};
use esp_idf_matter::matter::error::{Error, ErrorCode};

/// Matter Identify cluster ID (0x0003)
pub const CLUSTER_IDENTIFY: ClusterId = 0x0003;

const ATTR_IDENTIFY_TIME: AttrId = 0x0000;
const ATTR_IDENTIFY_TYPE: AttrId = 0x0001;
const CMD_IDENTIFY: u32 = 0x0000;

/// Identify cluster definition (mandatory for the Thermostat device type).
pub const IDENTIFY_CLUSTER: Cluster<'static> = Cluster::new(
    CLUSTER_IDENTIFY,
    4, // revision
    0, // feature_map (no optional features)
    &[
        Attribute::new(ATTR_IDENTIFY_TIME, Access::RA, Quality::N),
        Attribute::new(ATTR_IDENTIFY_TYPE, Access::RV, Quality::N),
    ],
    &[Command::new(CMD_IDENTIFY, None, Access::WO)],
    |_, _, _| true,
    |_, _, _| true,
);

/// Minimal Identify handler — tracks IdentifyTime, reports IdentifyType=None,
/// and accepts the Identify command as a no-op (no physical indicator on board).
pub struct IdentifyHandler {
    dataver: Dataver,
    identify_time: Cell<u16>,
}

impl IdentifyHandler {
    pub fn new(dataver: Dataver) -> Self {
        Self {
            dataver,
            identify_time: Cell::new(0),
        }
    }

    fn handle_read(&self, attr_id: AttrId, reply: impl Reply) -> Result<(), Error> {
        match attr_id {
            ATTR_IDENTIFY_TIME => reply.set(self.identify_time.get())?,
            ATTR_IDENTIFY_TYPE => reply.set(0u8)?, // 0 = None
            _ => return Err(ErrorCode::AttributeNotFound.into()),
        }
        Ok(())
    }
}

impl AsyncHandler for IdentifyHandler {
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
        if attr.cluster_id != CLUSTER_IDENTIFY {
            return Err(ErrorCode::ClusterNotFound.into());
        }
        if let Some(writer) = reply.with_dataver(self.dataver.get())? {
            self.handle_read(attr.attr_id, writer)?;
        }
        Ok(())
    }

    async fn write(&self, ctx: impl WriteContext) -> Result<(), Error> {
        let attr = ctx.attr();
        if attr.attr_id == ATTR_IDENTIFY_TIME {
            let t: u16 = ctx
                .data()
                .u16()
                .map_err(|_| Error::from(ErrorCode::InvalidDataType))?;
            self.identify_time.set(t);
            self.dataver.changed();
            ctx.notify_changed();
        }
        Ok(())
    }

    async fn invoke(
        &self,
        _ctx: impl InvokeContext,
        _reply: impl InvokeReply,
    ) -> Result<(), Error> {
        // Identify command — no-op (no LED/indicator to blink on this board).
        Ok(())
    }
}
