//! OTA (Over-The-Air) firmware update handlers.

use esp_idf_svc::hal::io::{EspIOError, Write};
use esp_idf_svc::http::server::EspHttpServer;
use esp_idf_svc::http::Method;
use esp_idf_svc::ota::EspOta;

/// Register OTA routes on the HTTP server.
pub fn register(server: &mut EspHttpServer<'static>) -> Result<(), Box<dyn std::error::Error>> {
    // GET /ota -- Upload form
    server.fn_handler("/ota", Method::Get, |req| {
        let html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>\
            <title>OTA Update</title>\
            <style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}\
            input[type=file]{margin:10px 0}\
            button{width:100%;padding:12px;background:#007aff;color:#fff;\
            border:none;border-radius:6px;font-size:16px}\
            #progress{display:none;margin:10px 0}</style></head>\
            <body><h1>OTA Update</h1>\
            <form id='f' method='POST' action='/ota' enctype='multipart/form-data'>\
            <input type='file' name='firmware' accept='.bin' required>\
            <br><button type='submit'>Upload</button></form>\
            <div id='progress'><p>Uploading... please wait</p></div>\
            <script>document.getElementById('f').onsubmit=function(){\
            document.getElementById('progress').style.display='block'}</script>\
            </body></html>";

        req.into_ok_response()?.write_all(html.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // POST /ota -- Receive firmware binary and flash it
    server.fn_handler("/ota", Method::Post, |mut req| {
        log::info!("OTA: upload started");

        let mut ota = match EspOta::new() {
            Ok(ota) => ota,
            Err(e) => {
                log::error!("OTA: init failed: {}", e);
                req.into_status_response(500)?
                    .write_all(b"OTA init failed")?;
                return Ok::<(), EspIOError>(());
            }
        };

        let mut update = match ota.initiate_update() {
            Ok(u) => u,
            Err(e) => {
                log::error!("OTA: begin failed: {}", e);
                req.into_status_response(500)?
                    .write_all(b"OTA begin failed")?;
                return Ok::<(), EspIOError>(());
            }
        };

        let mut buf = [0u8; 1024];
        let mut header_skipped = false;
        let mut total_written: usize = 0;

        loop {
            let len = match req.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => n,
                Err(e) => {
                    log::error!("OTA: read error: {}", e);
                    let _ = update.abort();
                    // Cannot send error response after partial read; just log
                    return Err(e);
                }
            };

            let mut data = &buf[..len];

            // Skip multipart header on first chunk (find \r\n\r\n)
            if !header_skipped {
                if let Some(pos) = find_header_end(&buf[..len]) {
                    data = &buf[pos..len];
                    header_skipped = true;
                } else {
                    // Header spans multiple chunks, skip this one
                    continue;
                }
            }

            if !data.is_empty() {
                if let Err(e) = update.write(data) {
                    log::error!("OTA: write failed: {}", e);
                    let _ = update.abort();
                    return Err(EspIOError(e));
                }
                total_written += data.len();
            }
        }

        log::info!("OTA: {} bytes written, finalizing...", total_written);

        match update.complete() {
            Ok(_) => {
                log::info!("OTA: update complete, restarting...");

                let html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>\
                    <title>OTA OK</title></head><body>\
                    <h1>Update successful!</h1><p>Restarting...</p></body></html>";

                req.into_ok_response()?.write_all(html.as_bytes())?;

                std::thread::sleep(std::time::Duration::from_secs(1));
                unsafe { esp_idf_svc::sys::esp_restart() };
            }
            Err(e) => {
                log::error!("OTA: complete failed: {}", e);
                req.into_status_response(500)?
                    .write_all(b"Image validation failed")?;
                Ok::<(), EspIOError>(())
            }
        }
    })?;

    Ok(())
}

/// Find the end of multipart headers (\r\n\r\n) and return the offset
/// to the start of the body.
fn find_header_end(data: &[u8]) -> Option<usize> {
    for i in 0..data.len().saturating_sub(3) {
        if data[i] == b'\r' && data[i + 1] == b'\n' && data[i + 2] == b'\r' && data[i + 3] == b'\n'
        {
            return Some(i + 4);
        }
    }
    None
}
