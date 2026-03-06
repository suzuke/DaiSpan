//! Swing control via Matter On/Off cluster.
//!
//! Uses the rs-matter built-in `OnOffHandler` with `OnOffHooks` to
//! control the Daikin swing (vertical axis). When toggled On, vertical
//! swing is enabled; Off disables it.

use core::cell::Cell;
use std::sync::mpsc::SyncSender;

use esp_idf_matter::matter::dm::clusters::on_off::{
    EffectVariantEnum, OnOffHooks, OutOfBandMessage, StartUpOnOffEnum,
};
use esp_idf_matter::matter::dm::clusters::decl::on_off as on_off_cluster;
use esp_idf_matter::matter::dm::Cluster;
use esp_idf_matter::matter::error::Error;
use esp_idf_matter::matter::tlv::Nullable;
use esp_idf_matter::matter::with;

use crate::controller::state::{ControllerCmd, SharedState};
use crate::s21::types::SwingAxis;

/// OnOffHooks implementation for swing control.
///
/// Reads current swing state from SharedState and sends SetSwing commands.
pub struct SwingOnOffLogic {
    state: SharedState,
    cmd_tx: SyncSender<ControllerCmd>,
    /// Local shadow in case SharedState isn't valid yet.
    on_off: Cell<bool>,
}

impl SwingOnOffLogic {
    pub fn new(state: SharedState, cmd_tx: SyncSender<ControllerCmd>) -> Self {
        Self {
            state,
            cmd_tx,
            on_off: Cell::new(false),
        }
    }
}

impl OnOffHooks for SwingOnOffLogic {
    const CLUSTER: Cluster<'static> = on_off_cluster::FULL_CLUSTER
        .with_revision(6)
        .with_attrs(with!(
            required;
            on_off_cluster::AttributeId::OnOff
        ))
        .with_cmds(with!(
            on_off_cluster::CommandId::Off
                | on_off_cluster::CommandId::On
                | on_off_cluster::CommandId::Toggle
        ));

    fn on_off(&self) -> bool {
        let status = self.state.read();
        if status.valid {
            status.swing_vertical
        } else {
            self.on_off.get()
        }
    }

    fn set_on_off(&self, on: bool) {
        self.on_off.set(on);
        let _ = self
            .cmd_tx
            .try_send(ControllerCmd::SetSwing(SwingAxis::Vertical, on));
        log::info!("Swing: vertical={}", on);
    }

    fn start_up_on_off(&self) -> Nullable<StartUpOnOffEnum> {
        // No startup behavior - just use previous state
        Nullable::none()
    }

    fn set_start_up_on_off(&self, _value: Nullable<StartUpOnOffEnum>) -> Result<(), Error> {
        // We don't persist startup state for swing
        Ok(())
    }

    async fn handle_off_with_effect(&self, _effect: EffectVariantEnum) {
        // No effects for swing
    }

    async fn run<F: Fn(OutOfBandMessage)>(&self, _notify: F) {
        // No background task needed; pend forever
        core::future::pending::<()>().await
    }
}
