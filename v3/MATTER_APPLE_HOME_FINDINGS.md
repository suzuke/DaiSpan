# v3 Matter on Apple Home — 調查記錄與現況

**日期**: 2026-06-22
**分支**: `v3/thermostat-fix`（off `v3/rust-matter-rewrite`）
**硬體**: ESP32-S3（16MB flash），非 C3

---

## TL;DR（先看這段，避免重走冤枉路）

v3（Rust / rs-matter）在 **ESP32-S3** 上**能成功配對進 Apple Home**，但 thermostat **無法顯示** — Apple Home 顯示「**家庭 App 目前不支援此配件**」。

經詳盡測試確認：**這是 `rs-matter` stack 層與 Apple Home thermostat conformance 的相容性問題，不是 device 或 cluster 配置的 bug。** device 在 Matter 標準層完全正確（配對成功、所有 attribute 讀取零錯誤）。

> **不要再花時間調 thermostat cluster 的 feature_map / attribute / Access / Quality / Identify / command — 全部試過了，連 Apple 官方驗證過能 render 的配置照抄都失敗。問題在 rs-matter 怎麼生成 Matter device 的底層，Apple 對 thermostat 的嚴格 conformance 不接受。**

要在 Apple Home 顯示恆溫器，可靠的路是把 Matter 層改用 **esp-matter (C++ SDK)** 重寫。或先用 **Home Assistant / Google Home** 驗證（那邊應該正常顯示）。

---

## 現況

| 項目 | 狀態 |
|------|------|
| Matter commissioning（配對） | ✅ 100% 成功（BLE → PASE → WiFi → CASE） |
| 加入 Apple Home fabric | ✅ 成功（fabric label 設定成功） |
| Apple 讀取 thermostat attributes | ✅ 全部、多輪、零錯誤 |
| Apple Home 顯示恆溫器控制 | ❌ 「不支援此配件」 |
| rs-matter light / fan 在 Apple | ✅ 能 render（對比組） |
| rs-matter thermostat 在 Apple | ❌ 無法 render |

---

## 平台：為什麼是 S3 不是 C3

ESP32-C3 是**記憶體死路**，無法跑完整 Matter commissioning：
- **concurrent 模式 (`run_coex`)**：BLE 在 WiFi 連線那一刻死掉（單天線 + 記憶體不足）→ `SND BTP !FAILED!: NoNetworkInterface` → commissioning 中斷
- **non-concurrent 模式 (`run`)**：64K thread stack 吃掉 heap → BLE bluedroid `calloc failed`（OOM）

ESP32-S3（512KB RAM）解決：用 **non-concurrent (`run`)** 模式，heap 充裕（idle ~61KB，BLE 關閉後 WiFi 階段 ~96KB），順利走完 commissioning。mod.rs 用 `#[cfg(feature = "esp32s3")]` 切換到 `run`。

---

## 配對關鍵設定（要重現先看這些）

- **WiFi 必須 2.4G**：S3 跟 C3 都只有 2.4GHz radio。iPhone 配對前要連 2.4G（commissioning 會把 iPhone 當前的 WiFi 傳給 device），連 5G 會 `Connecting to network SSID(...5G)` 失敗。
- **配對碼**：目前 `2392-8408-285`（discriminator 2748 / passcode 13572468，在 `mod.rs` 的 `run_matter` 內 runtime 建 `BasicCommData`）。
- **build**：`cd v3 && source ~/export-esp.sh && cargo build --release --no-default-features --features matter,esp32s3`（`.cargo/config.toml` 已是 S3 設定；C3 設定備份在 `.cargo/config.toml.c3.bak`）。
- **flash**：espflash 預設只給 1MB app partition（放不下 2.4MB binary），**必須明確指定 bootloader + partition-table**：
  ```
  espflash flash --port /dev/cu.usbmodem101 \
    --bootloader target/xtensa-esp32s3-espidf/release/build/esp-idf-sys-*/out/build/bootloader/bootloader.bin \
    --partition-table target/xtensa-esp32s3-espidf/release/build/esp-idf-sys-*/out/build/partition_table/partition-table.bin \
    target/xtensa-esp32s3-espidf/release/daispan
  ```
- **USB-Serial-JTAG 必須設為 PRIMARY console**（`sdkconfig.defaults.esp32s3`: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`）。S3 預設它只是 secondary console，WiFi/BLE 一啟動 secondary 就停止輸出，commissioning log 會整段消失（看起來像 crash，其實沒有）。
- **讀 serial**（macOS 無 `timeout`，espflash monitor headless 會失敗）：
  ```
  stty -f /dev/cu.usbmodem101 115200 raw; perl -e 'alarm 60; exec @ARGV' cat /dev/cu.usbmodem101
  ```
- **netif #22 IPv6 backport**：`esp-idf-matter` 248bf21 的 `netif.rs` 只保留最後一個 IPv6 位址，導致 operational `HostUnreachable`。已 backport commit a8a1b989（收集所有 IPv6 位址）到 `../esp-idf-matter-patched`，`Cargo.toml` 用 path dep 指過去。

---

## Apple 快取陷阱（重要）

Apple Home 會用 **device identity（VID/PID/discriminator）快取 accessory 的結構**。第一次配對若建立了某結構（如 fan+light），之後即使重燒成不同結構、Apple 也沿用舊快取。

**破解方法**：改 discriminator（3840 → 2748）讓 Apple 當成全新 device → 強制重建 accessory。改 discriminator 會改 pairing code（用 rs-matter 的 verhoeff 公式重算）。**注意：不要改 PID**（0x8001 → 0x800E 會讓 Apple 直接「不支援」，因為 PID 要對得上 TEST_DEV_ATT 的 Certification Declaration）。

---

## thermostat render 調查 — 全部試過、全部「不支援」

| 嘗試 | 結果 |
|------|------|
| feature_map 0x07 → 0x23（以為 AutoMode 是 bit2，其實 bit2=0x04 是 **Occupancy**，AutoMode 是 bit5=0x20）| ❌ |
| feature_map → 0x03（移除 OCC bit，避免缺 Occupancy attribute 的 conformance bug）| ❌ |
| 每個 attribute 的 Access::RA + Quality::N，照抄 known-working 參考 | ❌ |
| 移除 endpoint 1 的 Fan Control cluster（純 thermostat endpoint）| ❌ |
| AutoMode 雙設定點約束（heating < cooling + MinSetpointDeadBand）| ❌ |
| 破解 Apple 快取（新 discriminator）| ✅ 破解了快取，但 thermostat 仍「不支援」 |
| 單一 thermostat endpoint vs 多 endpoint | ❌ |
| 手寫補上 mandatory 的 **Identify cluster (0x0003)** | ❌ |
| **完全照抄 Apple 實測能 render 的 heat-only 配置**（home-assistant-matter-hub #497：feature 0x01、精簡 attr、ControlSeq=HeatingOnly、SystemMode=Heat、AcceptedCommandList=[0]）| ❌ 還是「不支援」 |

---

## 根本原因（結論）

**問題在 `rs-matter` 這個 Rust Matter stack 的底層，不在 thermostat cluster 配置。**

證據：
1. 連 **Apple 官方驗證過能 render 的配置**（#497）照抄都「不支援」→ 排除配置層
2. #497 能 render 是因為它用 **matter.js (TypeScript)**；我們用 **rs-matter (Rust)**，相同配置就不行 → 差異在 stack 生成 Matter device 的底層（global attribute 的 TLV 編碼 / descriptor 結構）
3. **關鍵不對稱**：rs-matter 的 OnOff light / fan 在 Apple 都 render 得出來，唯獨 thermostat 不行 → Apple 對 thermostat 的 conformance 檢查遠比 light/fan 嚴格，rs-matter 生成的某處剛好踩到 thermostat 的嚴格檢查
4. 查遍網路，**沒有任何人用 rs-matter 讓 thermostat 在 Apple Home render 成功過** — 這條路目前不通

Apple 端是黑盒；要拿到「具體缺什麼」只能開 [Apple Feedback Assistant](https://feedbackassistant.apple.com/) ticket（附 HomePod sysdiagnose + accessory/mDNS log）。

---

## 下一步建議

1. **先用 Home Assistant（python-matter-server）或 Google Home 驗證** — 恆溫器應該會正常顯示，證明 device 完全正常、純粹是 Apple ⨯ rs-matter 的限制，且立刻有可用的恆溫器控制。
2. **要 Apple Home 顯示恆溫器**：把 v3 的 Matter 層改用 **esp-matter (C++ SDK)** 重寫 — 成熟、被廣泛驗證、Apple thermostat 確定能 render。工程不小。
3. **或** 深入 fork rs-matter，對比它與 matter.js 生成的 global-attribute / descriptor 編碼差異 — 工程大且不保證找得到。

---

## 目前韌體狀態

- 燒的是**診斷用 heat-only 版**（thermostat.rs feature 0x01、只有暖氣 attribute；mod.rs NODE 只有 thermostat endpoint，fan/light endpoint 被註解掉）。
- **要恢復完整冷暖 + fan/light**：thermostat.rs 改回 feature 0x03 + 完整 attribute list；mod.rs NODE 取消註解 fan(ep2)/light(ep3) endpoint。但多 endpoint 在 Apple 會讓 thermostat 不 render（thermostat 部分本來就 render 不出來，多 endpoint 時 Apple 至少 render 電燈+風扇）。
- 已修好的程式碼（feature/Access/Quality/Identify/netif #22/identity/USB-JTAG console）都保留著，換 esp-matter 時這些 device 端的經驗（S3 移植、WiFi 2.4G、快取破解、flash 指令）仍適用。
