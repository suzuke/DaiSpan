//! HTTP route handlers for dashboard, WiFi config, API, and captive portal.

use std::sync::Arc;

use esp_idf_svc::hal::io::{EspIOError, Write};
use esp_idf_svc::http::server::EspHttpServer;
use esp_idf_svc::http::Method;

use super::WebContext;

/// URL-decode a string: %XX -> char, '+' -> ' '.
fn url_decode(input: &str) -> String {
    let mut result = String::with_capacity(input.len());
    let bytes = input.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            let hex = &input[i + 1..i + 3];
            if let Ok(val) = u8::from_str_radix(hex, 16) {
                result.push(val as char);
                i += 3;
                continue;
            }
        }
        if bytes[i] == b'+' {
            result.push(' ');
        } else {
            result.push(bytes[i] as char);
        }
        i += 1;
    }
    result
}

/// Extract a form field value from URL-encoded body.
fn form_field<'a>(body: &'a str, name: &str) -> Option<String> {
    let prefix = format!("{}=", name);
    let start = body.find(&prefix)?;
    let value_start = start + prefix.len();
    let value_end = body[value_start..].find('&').map(|p| value_start + p).unwrap_or(body.len());
    let raw = &body[value_start..value_end];
    Some(url_decode(raw))
}

/// Register all HTTP routes on the server.
pub fn register(
    server: &mut EspHttpServer<'static>,
    ctx: Arc<WebContext>,
) -> Result<(), Box<dyn std::error::Error>> {
    // GET / — Dashboard
    let c = ctx.clone();
    server.fn_handler("/", Method::Get, move |req| {
        let status = c.state.read();
        let uptime_secs = c.start_time.elapsed().as_secs();
        let hr = uptime_secs / 3600;
        let min = (uptime_secs % 3600) / 60;
        let sec = uptime_secs % 60;

        let heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };

        let power_str = if status.power { "ON" } else { "OFF" };
        let valid_str = if status.valid { "Healthy" } else { "No data" };

        let html = format!(
            "<!DOCTYPE html><html><head><meta charset='UTF-8'>\
             <meta http-equiv='refresh' content='30'>\
             <title>DaiSpan v3</title>\
             <style>body{{font-family:sans-serif;max-width:600px;margin:0 auto;padding:20px}}\
             .card{{background:#f5f5f5;border-radius:8px;padding:15px;margin:10px 0}}\
             .btn{{display:inline-block;padding:10px 20px;background:#007aff;color:#fff;\
             text-decoration:none;border-radius:6px;margin:5px}}</style></head><body>\
             <h1>DaiSpan v3</h1>\
             <div class='card'>\
             <p>Memory: {} bytes free</p>\
             <p>Uptime: {}:{:02}:{:02}</p>\
             <p>Power: {} | Target: {:.1}C | Current: {:.1}C</p>\
             <p>Mode: {:?} | Fan: {:?}</p>\
             <p>Status: {}</p>\
             </div>\
             <div><a class='btn' href='/wifi'>WiFi</a>\
             <a class='btn' href='/ota'>OTA</a>\
             <a class='btn' href='/api/health'>Health</a>\
             <a class='btn' href='/api/metrics'>Metrics</a></div>\
             </body></html>",
            heap, hr, min, sec,
            power_str, status.target_temp, status.current_temp,
            status.mode, status.fan_speed,
            valid_str,
        );

        req.into_ok_response()?.write_all(html.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // GET /wifi — WiFi config form
    server.fn_handler("/wifi", Method::Get, |req| {
        let html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>\
            <title>WiFi Config</title>\
            <style>body{font-family:sans-serif;max-width:400px;margin:0 auto;padding:20px}\
            input{width:100%;padding:10px;margin:5px 0;box-sizing:border-box;\
            border:1px solid #ccc;border-radius:4px}\
            button{width:100%;padding:12px;background:#007aff;color:#fff;\
            border:none;border-radius:6px;font-size:16px}</style></head>\
            <body><h1>WiFi Config</h1>\
            <form method='POST' action='/wifi-save'>\
            <label>SSID</label><input name='ssid' required>\
            <label>Password</label><input name='password' type='password'>\
            <br><button type='submit'>Save &amp; Restart</button>\
            </form></body></html>";

        req.into_ok_response()?.write_all(html.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // POST /wifi-save — Save WiFi credentials and restart
    let c = ctx.clone();
    server.fn_handler("/wifi-save", Method::Post, move |mut req| {
        let mut body = [0u8; 256];
        let len = req.read(&mut body)?;
        let body_str = std::str::from_utf8(&body[..len]).unwrap_or("");

        let ssid = form_field(body_str, "ssid").unwrap_or_default();
        let password = form_field(body_str, "password").unwrap_or_default();

        if ssid.is_empty() {
            req.into_status_response(400)?
                .write_all(b"SSID required")?;
            return Ok::<(), EspIOError>(());
        }

        log::info!("WiFi save: SSID='{}' pass_len={}", ssid, password.len());

        if let Ok(mut cfg) = c.config.lock() {
            let _ = cfg.set_wifi_credentials(&ssid, &password);
        }

        let html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>\
            <meta http-equiv='refresh' content='3;url=/'>\
            <title>Saved</title></head><body>\
            <h1>WiFi config saved!</h1><p>Restarting...</p></body></html>";

        req.into_ok_response()?.write_all(html.as_bytes())?;

        // Restart after response is sent
        std::thread::sleep(std::time::Duration::from_secs(1));
        unsafe { esp_idf_svc::sys::esp_restart() };
    })?;

    // GET /api/health — JSON health endpoint
    let c = ctx.clone();
    server.fn_handler("/api/health", Method::Get, move |req| {
        let heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };
        let uptime = c.start_time.elapsed().as_secs();

        let json = format!(
            "{{\"status\":\"ok\",\"freeHeap\":{},\"uptime\":{}}}",
            heap, uptime
        );

        let mut resp = req.into_response(200, None, &[("Content-Type", "application/json")])?;
        resp.write_all(json.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // GET /api/metrics — JSON metrics endpoint
    let c = ctx.clone();
    server.fn_handler("/api/metrics", Method::Get, move |req| {
        let status = c.state.read();
        let heap = unsafe { esp_idf_svc::sys::esp_get_free_heap_size() };
        let min_heap = unsafe { esp_idf_svc::sys::esp_get_minimum_free_heap_size() };
        let uptime = c.start_time.elapsed().as_secs();

        let json = format!(
            "{{\
             \"freeHeap\":{},\
             \"minFreeHeap\":{},\
             \"uptime\":{},\
             \"power\":{},\
             \"mode\":{},\
             \"targetTemp\":{:.1},\
             \"currentTemp\":{:.1},\
             \"fanSpeed\":{},\
             \"swingV\":{},\
             \"swingH\":{},\
             \"valid\":{}\
             }}",
            heap,
            min_heap,
            uptime,
            status.power,
            status.mode as u8,
            status.target_temp,
            status.current_temp,
            status.fan_speed as u8,
            status.swing_vertical,
            status.swing_horizontal,
            status.valid,
        );

        let mut resp = req.into_response(200, None, &[("Content-Type", "application/json")])?;
        resp.write_all(json.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // Apple captive portal: GET /hotspot-detect.html
    server.fn_handler("/hotspot-detect.html", Method::Get, |req| {
        let html = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
        req.into_ok_response()?.write_all(html.as_bytes())?;
        Ok::<(), EspIOError>(())
    })?;

    // Android captive portal: GET /generate_204 -> redirect to /wifi
    server.fn_handler("/generate_204", Method::Get, |req| {
        let mut resp = req.into_response(302, None, &[("Location", "http://192.168.4.1/wifi")])?;
        resp.write_all(b"")?;
        Ok::<(), EspIOError>(())
    })?;

    Ok(())
}
