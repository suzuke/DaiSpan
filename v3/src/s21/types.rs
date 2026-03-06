use heapless::Vec;

// S21 frame constants
pub const STX: u8 = 0x02;
pub const ETX: u8 = 0x03;
pub const ACK: u8 = 0x06;
pub const NAK: u8 = 0x15;
pub const ACK_TIMEOUT_MS: u32 = 150;
pub const RESPONSE_TIMEOUT_MS: u32 = 500;
pub const MAX_PAYLOAD_LEN: usize = 16;

/// AC operating mode (S21 protocol values)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum AcMode {
    Auto = 0,
    Dry = 1,
    Cool = 2,
    Heat = 3,
    Fan = 6,
    Auto2 = 7,
}

impl AcMode {
    pub fn from_byte(b: u8) -> Option<Self> {
        match b {
            0 => Some(Self::Auto),
            1 => Some(Self::Dry),
            2 => Some(Self::Cool),
            3 => Some(Self::Heat),
            6 => Some(Self::Fan),
            7 => Some(Self::Auto2),
            _ => None,
        }
    }
}

/// Fan speed setting
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum FanSpeed {
    Auto = 0,
    Quiet = 1,
    Speed1 = 3,
    Speed2 = 4,
    Speed3 = 5,
    Speed4 = 6,
    Speed5 = 7,
}

impl FanSpeed {
    /// Convert to S21 protocol character: A=auto, B=quiet, 3-7=speed1-5
    pub fn to_s21_char(self) -> u8 {
        match self {
            Self::Auto => b'A',
            Self::Quiet => b'B',
            Self::Speed1 => b'3',
            Self::Speed2 => b'4',
            Self::Speed3 => b'5',
            Self::Speed4 => b'6',
            Self::Speed5 => b'7',
        }
    }

    /// Parse from S21 protocol character
    pub fn from_s21_char(c: u8) -> Option<Self> {
        match c {
            b'A' => Some(Self::Auto),
            b'B' => Some(Self::Quiet),
            b'3' => Some(Self::Speed1),
            b'4' => Some(Self::Speed2),
            b'5' => Some(Self::Speed3),
            b'6' => Some(Self::Speed4),
            b'7' => Some(Self::Speed5),
            _ => None,
        }
    }

    /// Convert to percentage (0-100) for HomeKit fan service
    pub fn to_percent(self) -> u8 {
        match self {
            Self::Auto => 0,
            Self::Quiet => 10,
            Self::Speed1 => 20,
            Self::Speed2 => 40,
            Self::Speed3 => 60,
            Self::Speed4 => 80,
            Self::Speed5 => 100,
        }
    }

    /// Convert from percentage (0-100) to nearest fan speed
    pub fn from_percent(pct: u8) -> Self {
        match pct {
            0 => Self::Auto,
            1..=15 => Self::Quiet,
            16..=30 => Self::Speed1,
            31..=50 => Self::Speed2,
            51..=70 => Self::Speed3,
            71..=90 => Self::Speed4,
            _ => Self::Speed5,
        }
    }
}

/// Swing axis
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SwingAxis {
    Vertical,
    Horizontal,
}

/// Current AC status as reported by S21 protocol
#[derive(Debug, Clone)]
pub struct AcStatus {
    pub power: bool,
    pub mode: AcMode,
    pub target_temp: f32,
    pub current_temp: f32,
    pub fan_speed: FanSpeed,
    pub swing_vertical: bool,
    pub swing_horizontal: bool,
    pub valid: bool,
}

impl Default for AcStatus {
    fn default() -> Self {
        Self {
            power: false,
            mode: AcMode::Auto,
            target_temp: 21.0,
            current_temp: 21.0,
            fan_speed: FanSpeed::Auto,
            swing_vertical: false,
            swing_horizontal: false,
            valid: false,
        }
    }
}

/// An S21 protocol frame (command + optional payload)
#[derive(Debug, Clone)]
pub struct S21Frame {
    pub cmd: [u8; 2],
    pub payload: Vec<u8, MAX_PAYLOAD_LEN>,
}

/// S21 protocol errors
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum S21Error {
    Timeout,
    Nak,
    InvalidFrame,
    ChecksumError,
    UartError,
}

/// Encode target temperature to S21 byte.
/// Formula: encoded = (temp - 18) * 2 + '@'
pub fn encode_target_temp(temp: f32) -> u8 {
    let offset = ((temp - 18.0) * 2.0 + 0.5) as i32;
    (b'@' as i32 + offset) as u8
}

/// Decode target temperature from S21 byte.
/// Formula: temp = 18.0 + (encoded - '@') * 0.5
pub fn decode_target_temp(encoded: u8) -> f32 {
    18.0 + (encoded as f32 - b'@' as f32) * 0.5
}

/// Decode room temperature from S21 sensor payload.
/// Payload format: 3 ASCII digits + sign char ('+' or '-').
/// Example: "251+" = 25.1, "100-" = -10.0
pub fn decode_room_temp(payload: &[u8]) -> Option<f32> {
    if payload.len() < 4 {
        return None;
    }

    // Validate digits
    for &b in &payload[0..3] {
        if b < b'0' || b > b'9' {
            return None;
        }
    }

    // Validate sign
    let sign = payload[3];
    if sign != b'+' && sign != b'-' {
        return None;
    }

    let raw = (payload[0] - b'0') as i32 * 100
        + (payload[1] - b'0') as i32 * 10
        + (payload[2] - b'0') as i32;

    let raw = if sign == b'-' { -raw } else { raw };
    let temp = raw as f32 * 0.1;

    if temp < -50.0 || temp > 100.0 {
        return None;
    }

    Some(temp)
}
