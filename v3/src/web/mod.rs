//! HTTP web server for status, configuration, and OTA updates.
//!
//! Ported from v2/main/web/web_server.c.

pub mod ota;
pub mod routes;

use std::sync::Arc;

use esp_idf_svc::http::server::{Configuration as HttpConfig, EspHttpServer};

use crate::config::Config;
use crate::controller::SharedState;

/// Shared context passed to all HTTP handlers.
pub struct WebContext {
    pub state: SharedState,
    pub config: Arc<std::sync::Mutex<Config>>,
    #[allow(dead_code)]
    pub ap_mode: bool,
    pub start_time: std::time::Instant,
}

/// Start the HTTP server and register all routes.
///
/// - AP mode: port 80
/// - STA mode: port 8080
///
/// The returned `EspHttpServer` must be kept alive.
pub fn start(
    state: SharedState,
    config: Arc<std::sync::Mutex<Config>>,
    ap_mode: bool,
) -> Result<EspHttpServer<'static>, Box<dyn std::error::Error>> {
    let port = if ap_mode { 80 } else { 8080 };

    let http_config = HttpConfig {
        http_port: port,
        max_uri_handlers: 16,
        stack_size: 6144,
        ..Default::default()
    };

    let mut server = EspHttpServer::new(&http_config)?;

    let ctx = Arc::new(WebContext {
        state,
        config,
        ap_mode,
        start_time: std::time::Instant::now(),
    });

    routes::register(&mut server, ctx.clone())?;
    ota::register(&mut server)?;

    log::info!("Web server started on port {}", port);
    Ok(server)
}
