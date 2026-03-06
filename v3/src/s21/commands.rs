//! High-level S21 commands for controlling the Daikin AC.
//!
//! Implements set/query operations on top of the UART driver,
//! matching the v2 C implementation.

use super::types::*;
use super::uart::S21Uart;

impl S21Uart<'_> {
    /// Set AC state via D1 command.
    ///
    /// On failure, waits 50ms and retries once.
    pub fn set_state(
        &self,
        power: bool,
        mode: AcMode,
        temp: f32,
        fan: FanSpeed,
    ) -> Result<(), S21Error> {
        let payload = [
            if power { b'1' } else { b'0' },
            b'0' + mode as u8,
            encode_target_temp(temp),
            fan.to_s21_char(),
        ];

        log::info!(
            "D1: power={} mode={:?} temp={:.1} fan={:?}",
            power,
            mode,
            temp,
            fan
        );

        let result = self.send_command([b'D', b'1'], &payload);
        if result.is_ok() {
            return Ok(());
        }

        // Retry once after 50ms
        std::thread::sleep(std::time::Duration::from_millis(50));
        self.send_command([b'D', b'1'], &payload)
    }

    /// Query AC status via F1 -> G1.
    ///
    /// Returns (power, mode, target_temp, fan_speed).
    pub fn query_status(&self) -> Result<(bool, AcMode, f32, FanSpeed), S21Error> {
        let frame = self.query([b'F', b'1'], &[])?;

        if frame.cmd != [b'G', b'1'] {
            log::warn!(
                "Unexpected status response: {}{}",
                frame.cmd[0] as char,
                frame.cmd[1] as char
            );
            return Err(S21Error::InvalidFrame);
        }

        if frame.payload.len() < 4 {
            log::warn!("Status payload too short: {} bytes", frame.payload.len());
            return Err(S21Error::InvalidFrame);
        }

        let power = frame.payload[0] == b'1';
        let mode = AcMode::from_byte(frame.payload[1] - b'0').unwrap_or(AcMode::Auto);
        let temp = decode_target_temp(frame.payload[2]);
        let fan = FanSpeed::from_s21_char(frame.payload[3]).unwrap_or(FanSpeed::Auto);

        log::info!(
            "Status: power={} mode={:?} temp={:.1} fan={:?}",
            power,
            mode,
            temp,
            fan
        );

        Ok((power, mode, temp, fan))
    }

    /// Query room temperature via RH -> SH.
    ///
    /// Returns room temperature in degrees Celsius.
    pub fn query_temperature(&self) -> Result<f32, S21Error> {
        let frame = self.query([b'R', b'H'], &[])?;

        if frame.cmd != [b'S', b'H'] {
            return Err(S21Error::InvalidFrame);
        }

        if frame.payload.len() < 4 {
            return Err(S21Error::InvalidFrame);
        }

        decode_room_temp(frame.payload.as_slice()).ok_or_else(|| {
            log::warn!("Failed to decode room temperature");
            S21Error::InvalidFrame
        })
    }

    /// Query swing state via F5 -> G5.
    ///
    /// Returns (vertical_enabled, horizontal_enabled).
    pub fn query_swing(&self) -> Result<(bool, bool), S21Error> {
        let frame = self.query([b'F', b'5'], &[])?;

        if frame.cmd != [b'G', b'5'] {
            return Err(S21Error::InvalidFrame);
        }

        if frame.payload.len() < 2 {
            return Err(S21Error::InvalidFrame);
        }

        let vertical = frame.payload[0] != b'0';
        let horizontal = frame.payload[1] != b'0';

        log::debug!("Swing: V={} H={}", vertical, horizontal);
        Ok((vertical, horizontal))
    }

    /// Set swing state via D5 command.
    ///
    /// Uses '?' for enabled, '0' for disabled.
    pub fn set_swing(&self, vertical: bool, horizontal: bool) -> Result<(), S21Error> {
        let payload = [
            if vertical { b'?' } else { b'0' },
            if horizontal { b'?' } else { b'0' },
            b'0',
            b'0',
        ];

        log::info!("D5: V={} H={}", vertical, horizontal);
        self.send_command([b'D', b'5'], &payload)
    }
}
