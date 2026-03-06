//! S21 UART driver for ESP-IDF.
//!
//! Wraps `esp_idf_hal::uart::UartDriver` for S21 protocol communication
//! at 2400 baud, 8 data bits, even parity, 2 stop bits.

use std::time::Duration;

use esp_idf_hal::delay::TickType;
use esp_idf_hal::gpio::{AnyIOPin, InputPin, OutputPin};
use esp_idf_hal::uart::{self, UartDriver};
use esp_idf_hal::units::Hertz;

use super::codec::{decode_frame, encode_frame};
use super::types::*;

/// S21 UART driver. Owns the UART peripheral and provides
/// frame-level send/receive with ACK handling.
pub struct S21Uart<'d> {
    driver: UartDriver<'d>,
}

impl<'d> S21Uart<'d> {
    /// Initialize UART for S21 protocol: 2400 baud, 8E2.
    pub fn new(
        uart: impl uart::Uart + 'd,
        tx_pin: impl OutputPin + 'd,
        rx_pin: impl InputPin + 'd,
    ) -> Result<Self, S21Error> {
        let config = uart::config::Config::new()
            .baudrate(Hertz(2400))
            .data_bits(uart::config::DataBits::DataBits8)
            .parity_even()
            .stop_bits(uart::config::StopBits::STOP2)
            .flow_control(uart::config::FlowControl::None);

        let driver = UartDriver::new(
            uart,
            tx_pin,
            rx_pin,
            None::<AnyIOPin>,
            None::<AnyIOPin>,
            &config,
        )
        .map_err(|_| S21Error::UartError)?;

        log::info!("S21 UART initialized: 2400 baud, 8E2");
        Ok(Self { driver })
    }

    /// Send a command frame and wait for ACK.
    ///
    /// Builds the frame via codec, flushes RX, writes, then reads
    /// a single ACK/NAK byte with 150ms timeout.
    pub fn send_command(&self, cmd: [u8; 2], payload: &[u8]) -> Result<(), S21Error> {
        let frame = encode_frame(cmd, payload);

        // Flush any stale RX data
        self.driver.clear_rx().map_err(|_| S21Error::UartError)?;

        // Write frame
        self.driver
            .write(frame.as_slice())
            .map_err(|_| S21Error::UartError)?;

        // Wait for ACK (single byte, 150ms timeout)
        let mut ack_buf = [0u8; 1];
        let timeout = TickType::from(Duration::from_millis(ACK_TIMEOUT_MS as u64));
        let read = self
            .driver
            .read(&mut ack_buf, timeout.ticks())
            .map_err(|_| S21Error::UartError)?;

        if read == 0 {
            log::warn!(
                "ACK timeout for {}{}",
                cmd[0] as char,
                cmd[1] as char
            );
            return Err(S21Error::Timeout);
        }

        match ack_buf[0] {
            ACK => Ok(()),
            NAK => {
                log::warn!(
                    "NAK received for {}{}",
                    cmd[0] as char,
                    cmd[1] as char
                );
                Err(S21Error::Nak)
            }
            other => {
                log::warn!(
                    "Unexpected ACK byte 0x{:02X} for {}{}",
                    other,
                    cmd[0] as char,
                    cmd[1] as char
                );
                Err(S21Error::InvalidFrame)
            }
        }
    }

    /// Read a response frame from UART.
    ///
    /// Reads bytes one at a time until ETX is found (500ms total timeout),
    /// decodes the frame, and sends ACK back.
    pub fn read_response(&self) -> Result<S21Frame, S21Error> {
        let mut buf = [0u8; 64];
        let mut total = 0usize;
        let start = std::time::Instant::now();
        let timeout = Duration::from_millis(RESPONSE_TIMEOUT_MS as u64);

        while total < buf.len() {
            let remaining = timeout.saturating_sub(start.elapsed());
            if remaining.is_zero() {
                log::warn!("Response timeout after {} bytes", total);
                return Err(S21Error::Timeout);
            }

            // Read one byte at a time with partial timeout
            let chunk_timeout = remaining.min(Duration::from_millis(100));
            let tick_timeout = TickType::from(chunk_timeout);
            let read = self
                .driver
                .read(&mut buf[total..total + 1], tick_timeout.ticks())
                .map_err(|_| S21Error::UartError)?;

            if read == 0 {
                continue;
            }

            total += 1;

            if buf[total - 1] == ETX {
                break;
            }
        }

        if total < 5 {
            log::warn!("Response too short: {} bytes", total);
            return Err(S21Error::InvalidFrame);
        }

        // Decode the frame
        let frame = decode_frame(&buf[..total])?;

        // Send ACK back
        self.driver
            .write(&[ACK])
            .map_err(|_| S21Error::UartError)?;

        Ok(frame)
    }

    /// Send a query command and read the response.
    ///
    /// Convenience method combining `send_command` + `read_response`.
    pub fn query(&self, cmd: [u8; 2], payload: &[u8]) -> Result<S21Frame, S21Error> {
        self.send_command(cmd, payload)?;
        self.read_response()
    }
}
