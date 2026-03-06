//! S21 protocol version detection.
//!
//! Types (`S21Version`, `S21Features`) are available on all platforms.
//! The probing methods on `S21Uart` are gated behind `target_os = "espidf"`.

use std::fmt;

/// S21 protocol version, ordered from oldest to newest.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum S21Version {
    /// Basic protocol, only F1/G1 status queries
    V1,
    /// Extended protocol with F8 support
    V2,
    /// Modern protocol with version reporting (FY00 -> "3.xx")
    V3 { major: u8, minor: u8 },
    /// Future protocol version
    V4,
}

impl S21Version {
    /// Whether this version supports FX extended commands (v3.20+).
    pub fn supports_fx(&self) -> bool {
        match self {
            Self::V3 { major: 3, minor } => *minor >= 20,
            Self::V3 { major, .. } => *major > 3,
            Self::V4 => true,
            _ => false,
        }
    }

    /// Whether this version supports FK advanced feature queries (v3.10+).
    pub fn supports_fk(&self) -> bool {
        match self {
            Self::V3 { major: 3, minor } => *minor >= 10,
            Self::V3 { major, .. } => *major > 3,
            Self::V4 => true,
            _ => false,
        }
    }

    /// Whether this version supports F2 feature queries (v3.00+).
    pub fn supports_f2(&self) -> bool {
        matches!(self, Self::V3 { .. } | Self::V4)
    }
}

impl fmt::Display for S21Version {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::V1 => write!(f, "V1"),
            Self::V2 => write!(f, "V2"),
            Self::V3 { major, minor } => write!(f, "V{}.{:02}", major, minor),
            Self::V4 => write!(f, "V4"),
        }
    }
}

/// Feature capabilities detected from the AC unit.
#[derive(Debug, Clone, Default)]
pub struct S21Features {
    // Basic modes (V1+)
    pub has_cool_mode: bool,
    pub has_heat_mode: bool,
    pub has_temperature_display: bool,

    // Extended modes (V2+)
    pub has_auto_mode: bool,
    pub has_dehumidify: bool,
    pub has_fan_mode: bool,
    pub has_error_reporting: bool,

    // Enhanced modes (V3+ via F2)
    pub has_powerful_mode: bool,
    pub has_eco_mode: bool,
    pub has_quiet_mode: bool,
    pub has_comfort_mode: bool,

    // Sensors and controls (V3+ via F2)
    pub has_swing_control: bool,
    pub has_schedule_mode: bool,
    pub has_humidity_sensor: bool,
    pub has_outdoor_temp_sensor: bool,

    // Advanced features (V3.10+ via FK)
    pub has_multi_zone: bool,
    pub has_wifi_module: bool,
    pub has_advanced_filters: bool,
    pub has_energy_monitoring: bool,

    // Latest features (V3.20+ via FX)
    pub has_maintenance_alerts: bool,
    pub has_remote_diagnostics: bool,
}

/// Version detection and feature probing methods on S21Uart.
#[cfg(target_os = "espidf")]
mod probe {
    use super::*;
    use crate::s21::uart::S21Uart;

    impl S21Uart<'_> {
        /// Detect the S21 protocol version by progressive probing.
        ///
        /// Probe order: FY00 (v3+) -> FU (v3 variant) -> F8 (v2) -> F1 (v1).
        /// If all probes fail, assumes V1.
        pub fn detect_version(&self) -> S21Version {
            log::info!("Detecting S21 protocol version...");

            // Step 1: Try FY00 command (V3+ version detection)
            if let Ok(frame) = self.query([b'F', b'Y'], &[b'0', b'0']) {
                if frame.cmd == [b'G', b'Y'] && frame.payload.len() >= 4 {
                    let payload = frame.payload.as_slice();
                    // Parse version format: "3.xx" or "4.xx"
                    if payload[0] >= b'3' && payload[0] <= b'9' && payload[1] == b'.' {
                        let major = payload[0] - b'0';
                        let mut minor = (payload[2] - b'0') * 10;
                        if frame.payload.len() >= 5
                            && payload[3] >= b'0'
                            && payload[3] <= b'9'
                        {
                            minor += payload[3] - b'0';
                        }

                        if major >= 4 {
                            log::info!(
                                "Detected S21 protocol V4 (reported {}.{:02})",
                                major,
                                minor
                            );
                            return S21Version::V4;
                        }

                        log::info!("Detected S21 protocol V{}.{:02}", major, minor);
                        return S21Version::V3 { major, minor };
                    }
                }
            }

            // Step 2: Try FU command (some V3 variants)
            log::info!("FY failed, trying FU for V3 variant...");
            if let Ok(frame) = self.query([b'F', b'U'], &[]) {
                if frame.cmd == [b'G', b'U'] {
                    log::info!("Detected S21 protocol V3.00 (via FU)");
                    return S21Version::V3 {
                        major: 3,
                        minor: 0,
                    };
                }
            }

            // Step 3: Try F8 command (V2 detection)
            log::info!("FU failed, trying F8 for V2...");
            if let Ok(frame) = self.query([b'F', b'8'], &[]) {
                if frame.cmd == [b'G', b'8'] {
                    log::info!("Detected S21 protocol V2");
                    return S21Version::V2;
                }
            }

            // Step 4: Try basic F1 command (V1 detection)
            log::info!("F8 failed, trying F1 for V1...");
            if let Ok(frame) = self.query([b'F', b'1'], &[]) {
                if frame.cmd == [b'G', b'1'] {
                    log::info!("Detected S21 protocol V1");
                    return S21Version::V1;
                }
            }

            // All failed, assume V1
            log::warn!("All version probes failed, assuming V1");
            S21Version::V1
        }

        /// Detect features based on version and additional probing.
        ///
        /// Sets baseline features from version, then probes F2/FK/FX
        /// for detailed capability flags.
        pub fn detect_features(&self, version: S21Version) -> S21Features {
            let mut features = S21Features::default();

            // V1+ baseline
            if version >= S21Version::V1 {
                features.has_cool_mode = true;
                features.has_heat_mode = true;
                features.has_temperature_display = true;
            }

            // V2+ features
            if version >= S21Version::V2 {
                features.has_auto_mode = true;
                features.has_dehumidify = true;
                features.has_fan_mode = true;
                features.has_error_reporting = true;
            }

            // V3+ probing
            if version.supports_f2() {
                self.probe_f2_features(&mut features);
            }

            if version.supports_fk() {
                self.probe_fk_features(&mut features);
            }

            if version.supports_fx() {
                self.probe_fx_commands(&mut features);
            }

            log::info!("Features detected for {}: {:?}", version, features);
            features
        }

        /// Probe F2 command for basic V3 feature flags.
        fn probe_f2_features(&self, features: &mut S21Features) {
            if let Ok(frame) = self.query([b'F', b'2'], &[]) {
                if frame.cmd == [b'G', b'2'] && !frame.payload.is_empty() {
                    let p0 = frame.payload[0];
                    features.has_powerful_mode = (p0 & 0x01) != 0;
                    features.has_eco_mode = (p0 & 0x02) != 0;
                    features.has_quiet_mode = (p0 & 0x04) != 0;
                    features.has_comfort_mode = (p0 & 0x08) != 0;

                    if frame.payload.len() >= 2 {
                        let p1 = frame.payload[1];
                        features.has_swing_control = (p1 & 0x01) != 0;
                        features.has_schedule_mode = (p1 & 0x02) != 0;
                        features.has_humidity_sensor = (p1 & 0x04) != 0;
                        features.has_outdoor_temp_sensor = (p1 & 0x08) != 0;
                    }
                }
            }
        }

        /// Probe FK command for advanced features (V3.10+).
        fn probe_fk_features(&self, features: &mut S21Features) {
            if let Ok(frame) = self.query([b'F', b'K'], &[]) {
                if frame.cmd == [b'G', b'K'] && !frame.payload.is_empty() {
                    let p0 = frame.payload[0];
                    features.has_multi_zone = (p0 & 0x01) != 0;
                    features.has_wifi_module = (p0 & 0x02) != 0;
                    features.has_advanced_filters = (p0 & 0x04) != 0;
                    features.has_energy_monitoring = (p0 & 0x08) != 0;
                }
            }
        }

        /// Probe FX command types for latest features (V3.20+).
        pub fn probe_fx_commands(&self, features: &mut S21Features) {
            if let Ok(frame) = self.query([b'F', b'X'], &[b'0', b'0']) {
                if frame.cmd == [b'G', b'X'] && !frame.payload.is_empty() {
                    let p0 = frame.payload[0];
                    features.has_maintenance_alerts = (p0 & 0x01) != 0;
                    features.has_remote_diagnostics = (p0 & 0x02) != 0;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_ordering() {
        assert!(S21Version::V1 < S21Version::V2);
        assert!(S21Version::V2 < S21Version::V3 { major: 3, minor: 0 });
        assert!(
            S21Version::V3 { major: 3, minor: 0 }
                < S21Version::V3 {
                    major: 3,
                    minor: 20
                }
        );
        assert!(
            S21Version::V3 {
                major: 3,
                minor: 50
            } < S21Version::V4
        );
    }

    #[test]
    fn test_supports_fx() {
        assert!(!S21Version::V1.supports_fx());
        assert!(!S21Version::V2.supports_fx());
        assert!(!S21Version::V3 { major: 3, minor: 10 }.supports_fx());
        assert!(S21Version::V3 { major: 3, minor: 20 }.supports_fx());
        assert!(S21Version::V3 { major: 3, minor: 50 }.supports_fx());
        assert!(S21Version::V4.supports_fx());
    }

    #[test]
    fn test_supports_fk() {
        assert!(!S21Version::V1.supports_fk());
        assert!(!S21Version::V3 { major: 3, minor: 0 }.supports_fk());
        assert!(S21Version::V3 { major: 3, minor: 10 }.supports_fk());
        assert!(S21Version::V4.supports_fk());
    }

    #[test]
    fn test_supports_f2() {
        assert!(!S21Version::V1.supports_f2());
        assert!(!S21Version::V2.supports_f2());
        assert!(S21Version::V3 { major: 3, minor: 0 }.supports_f2());
        assert!(S21Version::V4.supports_f2());
    }

    #[test]
    fn test_display() {
        assert_eq!(format!("{}", S21Version::V1), "V1");
        assert_eq!(format!("{}", S21Version::V2), "V2");
        assert_eq!(
            format!("{}", S21Version::V3 { major: 3, minor: 20 }),
            "V3.20"
        );
        assert_eq!(format!("{}", S21Version::V4), "V4");
    }

    #[test]
    fn test_features_default() {
        let f = S21Features::default();
        assert!(!f.has_cool_mode);
        assert!(!f.has_auto_mode);
        assert!(!f.has_powerful_mode);
    }
}
