//! Shared state and command types for the controller.

use std::sync::{Arc, Mutex};

use crate::s21::types::{AcMode, AcStatus, FanSpeed, SwingAxis};

/// Commands sent from HomeKit/API to the controller task.
#[derive(Debug, Clone)]
pub enum ControllerCmd {
    SetPower(bool),
    SetMode(MatterMode),
    SetTemp(f32),
    SetFan(FanSpeed),
    SetSwing(SwingAxis, bool),
}

/// Matter/HomeKit thermostat mode values.
///
/// These differ from S21 AC modes -- the controller handles
/// the lossy mapping between them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum MatterMode {
    Off = 0,
    Auto = 1,
    Cool = 3,
    Heat = 4,
}

impl MatterMode {
    /// Convert Matter mode to S21 AC mode.
    /// Returns None for Off (handled as power=false instead).
    pub fn to_ac_mode(self) -> Option<AcMode> {
        match self {
            Self::Off => None,
            Self::Auto => Some(AcMode::Auto),
            Self::Cool => Some(AcMode::Cool),
            Self::Heat => Some(AcMode::Heat),
        }
    }

    /// Convert S21 AC mode to the closest Matter mode.
    /// Lossy: Dry and Fan map to Auto (no equivalent in Matter).
    pub fn from_ac_mode(mode: AcMode) -> Self {
        match mode {
            AcMode::Cool => Self::Cool,
            AcMode::Heat => Self::Heat,
            AcMode::Auto | AcMode::Auto2 => Self::Auto,
            // Dry and Fan have no Matter equivalent; preserve as Auto
            AcMode::Dry | AcMode::Fan => Self::Auto,
        }
    }

    /// Parse from u8 value.
    pub fn from_u8(val: u8) -> Option<Self> {
        match val {
            0 => Some(Self::Off),
            1 => Some(Self::Auto),
            3 => Some(Self::Cool),
            4 => Some(Self::Heat),
            _ => None,
        }
    }
}

/// Thread-safe shared AC status, read by HomeKit and updated by controller.
#[derive(Clone)]
pub struct SharedState {
    inner: Arc<Mutex<AcStatus>>,
}

impl SharedState {
    /// Create a new shared state with default values.
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(AcStatus::default())),
        }
    }

    /// Read a snapshot of the current AC status.
    pub fn read(&self) -> AcStatus {
        self.inner.lock().unwrap().clone()
    }

    /// Update the shared status from the controller's local copy.
    pub fn update(&self, status: &AcStatus) {
        let mut guard = self.inner.lock().unwrap();
        *guard = status.clone();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_matter_mode_roundtrip() {
        // Lossless roundtrips
        assert_eq!(
            MatterMode::from_ac_mode(MatterMode::Cool.to_ac_mode().unwrap()),
            MatterMode::Cool
        );
        assert_eq!(
            MatterMode::from_ac_mode(MatterMode::Heat.to_ac_mode().unwrap()),
            MatterMode::Heat
        );
        assert_eq!(
            MatterMode::from_ac_mode(MatterMode::Auto.to_ac_mode().unwrap()),
            MatterMode::Auto
        );
    }

    #[test]
    fn test_matter_mode_lossy() {
        // Dry/Fan -> Auto (lossy)
        assert_eq!(MatterMode::from_ac_mode(AcMode::Dry), MatterMode::Auto);
        assert_eq!(MatterMode::from_ac_mode(AcMode::Fan), MatterMode::Auto);
    }

    #[test]
    fn test_matter_mode_off() {
        assert!(MatterMode::Off.to_ac_mode().is_none());
    }

    #[test]
    fn test_matter_mode_from_u8() {
        assert_eq!(MatterMode::from_u8(0), Some(MatterMode::Off));
        assert_eq!(MatterMode::from_u8(1), Some(MatterMode::Auto));
        assert_eq!(MatterMode::from_u8(3), Some(MatterMode::Cool));
        assert_eq!(MatterMode::from_u8(4), Some(MatterMode::Heat));
        assert_eq!(MatterMode::from_u8(2), None);
        assert_eq!(MatterMode::from_u8(5), None);
    }

    #[test]
    fn test_shared_state() {
        let state = SharedState::new();
        let s = state.read();
        assert!(!s.valid);
        assert!(!s.power);

        let mut updated = s.clone();
        updated.power = true;
        updated.valid = true;
        updated.target_temp = 25.0;
        state.update(&updated);

        let s2 = state.read();
        assert!(s2.power);
        assert!(s2.valid);
        assert!((s2.target_temp - 25.0).abs() < 0.01);
    }

    #[test]
    fn test_shared_state_clone() {
        let state = SharedState::new();
        let state2 = state.clone();

        let mut updated = AcStatus::default();
        updated.power = true;
        state.update(&updated);

        // Both clones see the same data
        assert!(state2.read().power);
    }
}
