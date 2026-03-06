//! DNS captive portal server for AP mode.
//!
//! Responds to ALL DNS queries with 192.168.4.1 so that
//! phones/laptops auto-open the WiFi config page.

use std::net::UdpSocket;

/// Spawn the DNS captive portal server thread.
///
/// Binds UDP port 53 and responds to every DNS query with an
/// A record pointing to 192.168.4.1 (TTL 10s).
pub fn start_dns_server() {
    std::thread::Builder::new()
        .name("dns_srv".into())
        .stack_size(4096)
        .spawn(dns_server_task)
        .expect("Failed to spawn DNS server thread");
}

fn dns_server_task() {
    let sock = match UdpSocket::bind("0.0.0.0:53") {
        Ok(s) => s,
        Err(e) => {
            log::error!("DNS: bind failed: {}", e);
            return;
        }
    };

    log::info!("DNS captive portal server started on :53");

    let mut buf = [0u8; 512];

    loop {
        let (len, src) = match sock.recv_from(&mut buf) {
            Ok(r) => r,
            Err(e) => {
                log::warn!("DNS: recv error: {}", e);
                continue;
            }
        };

        // DNS header is 12 bytes minimum
        if len < 12 {
            continue;
        }

        // Build response
        let mut resp = [0u8; 512];
        resp[..len].copy_from_slice(&buf[..len]);

        // Set flags: QR=1 (response), AA=1 (authoritative), RD preserved, RA=1
        // 0x84 = 1_0000_1_0_0 = QR=1, Opcode=0, AA=1, TC=0, RD=0
        // But we want to preserve RD from query, so:
        resp[2] = 0x84 | (buf[2] & 0x01); // preserve RD bit
        resp[3] = 0x80; // RA=1, RCODE=0

        // Answer count = 1
        resp[6] = 0x00;
        resp[7] = 0x01;

        // Append answer record after the query
        let mut pos = len;

        // Name pointer to question name (offset 0x0C = 12, start of question section)
        resp[pos] = 0xC0;
        resp[pos + 1] = 0x0C;
        pos += 2;

        // Type A (1)
        resp[pos] = 0x00;
        resp[pos + 1] = 0x01;
        pos += 2;

        // Class IN (1)
        resp[pos] = 0x00;
        resp[pos + 1] = 0x01;
        pos += 2;

        // TTL = 10 seconds
        resp[pos] = 0x00;
        resp[pos + 1] = 0x00;
        resp[pos + 2] = 0x00;
        resp[pos + 3] = 0x0A;
        pos += 4;

        // RDLENGTH = 4 (IPv4 address)
        resp[pos] = 0x00;
        resp[pos + 1] = 0x04;
        pos += 2;

        // RDATA = 192.168.4.1
        resp[pos] = 192;
        resp[pos + 1] = 168;
        resp[pos + 2] = 4;
        resp[pos + 3] = 1;
        pos += 4;

        if let Err(e) = sock.send_to(&resp[..pos], src) {
            log::warn!("DNS: send error: {}", e);
        }
    }
}
