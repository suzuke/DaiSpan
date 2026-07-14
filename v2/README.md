# DaiSpan v2

DaiSpan v2 is the active ESP-IDF/FreeRTOS firmware. The archived Arduino/HomeSpan
implementation at the repository root is not part of this build.

## Provisioning

Monitor the first boot at 115200 baud. The firmware generates and stores two
per-device credentials in NVS:

- A 32-character administrator token. It is both the `DaiSpan-Config` WPA
  password and the password for HTTP Basic authentication with user `admin`.
- A random HomeKit setup code.

Each new credential is printed once when it is created. Store both credentials
securely. Erasing NVS generates new credentials and removes the saved Wi-Fi
configuration.

In provisioning mode, connect to `DaiSpan-Config` and open `http://192.168.4.1`.
Only Wi-Fi provisioning and captive-portal routes are available. After the
device joins the configured network, the administrator UI is available at
`http://<device-ip>:8080`; OTA and metrics require the administrator credential.
`/api/health` remains unauthenticated for local liveness checks.

## Build and host tests

The current build expects ESP-IDF 5.3 and the ESP HomeKit SDK under
`~/esp/esp-homekit-sdk`.

```sh
source ~/esp/esp-idf/export.sh
idf.py build

cmake -S tests/host -B /tmp/daispan-v2-host-tests
cmake --build /tmp/daispan-v2-host-tests
ctest --test-dir /tmp/daispan-v2-host-tests --output-on-failure
```

## Production security boundary

The local administrator interface currently uses HTTP Basic authentication over
plain HTTP. Use it only on a trusted network. Secure Boot, signed-image policy,
Flash/NVS encryption, and HTTPS require per-device manufacturing keys and are
not enabled by the development defaults.
