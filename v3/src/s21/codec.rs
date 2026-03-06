use heapless::Vec;

use super::types::{S21Error, S21Frame, STX, ETX, MAX_PAYLOAD_LEN};

/// Compute checksum: wrapping sum of all bytes.
pub fn checksum(data: &[u8]) -> u8 {
    data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b))
}

/// Encode an S21 frame: STX + cmd[0] + cmd[1] + payload + checksum + ETX.
/// Checksum covers bytes between STX and ETX (exclusive of both).
pub fn encode_frame(cmd: [u8; 2], payload: &[u8]) -> Vec<u8, 32> {
    let mut frame = Vec::<u8, 32>::new();
    let _ = frame.push(STX);
    let _ = frame.push(cmd[0]);
    let _ = frame.push(cmd[1]);
    for &b in payload {
        let _ = frame.push(b);
    }
    // Checksum covers cmd + payload (everything between STX and checksum/ETX)
    let cksum = checksum(&frame[1..]);
    let _ = frame.push(cksum);
    let _ = frame.push(ETX);
    frame
}

/// Decode an S21 frame from a byte buffer.
/// Minimum 5 bytes: STX + cmd0 + cmd1 + checksum + ETX.
pub fn decode_frame(buf: &[u8]) -> Result<S21Frame, S21Error> {
    if buf.len() < 5 {
        return Err(S21Error::InvalidFrame);
    }

    if buf[0] != STX {
        return Err(S21Error::InvalidFrame);
    }

    if buf[buf.len() - 1] != ETX {
        return Err(S21Error::InvalidFrame);
    }

    // Checksum is second-to-last byte, covers everything between STX and checksum
    let cksum_idx = buf.len() - 2;
    let expected = checksum(&buf[1..cksum_idx]);
    let actual = buf[cksum_idx];

    if expected != actual {
        return Err(S21Error::ChecksumError);
    }

    let cmd = [buf[1], buf[2]];
    let payload_bytes = &buf[3..cksum_idx];
    let mut payload = Vec::<u8, MAX_PAYLOAD_LEN>::new();
    for &b in payload_bytes {
        payload.push(b).map_err(|_| S21Error::InvalidFrame)?;
    }

    Ok(S21Frame { cmd, payload })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::s21::types::*;

    #[test]
    fn test_encode_decode_roundtrip() {
        let cmd = [b'F', b'1'];
        let payload = [b'A', b'B', b'C'];
        let frame = encode_frame(cmd, &payload);
        let decoded = decode_frame(&frame).unwrap();
        assert_eq!(decoded.cmd, cmd);
        assert_eq!(decoded.payload.as_slice(), &payload);
    }

    #[test]
    fn test_encode_d1_command() {
        let cmd = [b'D', b'1'];
        let payload = [b'1', b'2', b'F', b'A'];
        let frame = encode_frame(cmd, &payload);

        // Verify STX at start, ETX at end
        assert_eq!(frame[0], STX);
        assert_eq!(frame[frame.len() - 1], ETX);

        // Verify roundtrip
        let decoded = decode_frame(&frame).unwrap();
        assert_eq!(decoded.cmd, cmd);
        assert_eq!(decoded.payload.as_slice(), &payload);
    }

    #[test]
    fn test_bad_checksum() {
        let cmd = [b'F', b'1'];
        let frame = encode_frame(cmd, &[]);
        let mut corrupted: std::vec::Vec<u8> = frame.iter().copied().collect();
        // Corrupt the checksum byte (second to last)
        let cksum_idx = corrupted.len() - 2;
        corrupted[cksum_idx] = corrupted[cksum_idx].wrapping_add(1);

        let result = decode_frame(&corrupted);
        assert_eq!(result.unwrap_err(), S21Error::ChecksumError);
    }

    #[test]
    fn test_temp_encoding() {
        let temps = [18.0f32, 21.0, 25.5, 30.0];
        for temp in &temps {
            let encoded = encode_target_temp(*temp);
            let decoded = decode_target_temp(encoded);
            assert!(
                (decoded - temp).abs() < 0.01,
                "temp {} encoded to {} decoded to {}",
                temp,
                encoded,
                decoded
            );
        }
    }

    #[test]
    fn test_room_temp_decode() {
        // "251+" = 25.1
        assert!((decode_room_temp(b"251+").unwrap() - 25.1).abs() < 0.01);
        // "100-" = -10.0
        assert!((decode_room_temp(b"100-").unwrap() - (-10.0)).abs() < 0.01);
        // "000+" = 0.0
        assert!((decode_room_temp(b"000+").unwrap() - 0.0).abs() < 0.01);
        // Short input
        assert!(decode_room_temp(b"25").is_none());
    }

    #[test]
    fn test_fan_speed_roundtrip() {
        let speeds = [
            FanSpeed::Auto,
            FanSpeed::Quiet,
            FanSpeed::Speed1,
            FanSpeed::Speed2,
            FanSpeed::Speed3,
            FanSpeed::Speed4,
            FanSpeed::Speed5,
        ];
        for speed in &speeds {
            let c = speed.to_s21_char();
            let back = FanSpeed::from_s21_char(c).unwrap();
            assert_eq!(*speed, back, "roundtrip failed for {:?} -> {}", speed, c);
        }
    }
}
