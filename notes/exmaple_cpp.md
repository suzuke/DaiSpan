#include "HomeSpan.h"         // HomeSpan sketches always begin by including the HomeSpan library
#include "daikin_s21.h"
#include <driver/uart.h>

// S21 通信相關常量
#define UART_NUM UART_NUM_1
#define TX_PIN  43  // ESP32-S3 的 TX 引腳
#define RX_PIN  44  // ESP32-S3 的 RX 引腳
#define BUF_SIZE 256

// S21 狀態
struct {
  bool power;
  uint8_t mode;
  float temp;
  uint8_t fan;
  bool swingV;
  bool swingH;
  float roomTemp;
  float outdoorTemp;
  uint32_t lastPoll;
  uint8_t retries;
} s21;

// S21 狀態結構體
struct S21State {
  bool power;
  uint8_t mode;
  float temp;
  uint8_t fan;
  bool swingV;
};

// S21 空調服務
struct S21AC : Service::HeaterCooler {
  // HomeKit 特性
  SpanCharacteristic *active;              // 必需：開關狀態
  SpanCharacteristic *currentTemp;         // 必需：當前溫度
  SpanCharacteristic *currentMode;         // 必需：當前模式
  SpanCharacteristic *targetMode;          // 必需：目標模式
  SpanCharacteristic *targetTemp;          // 可選：製冷目標溫度
  SpanCharacteristic *heatingTemp;         // 可選：製熱目標溫��
  SpanCharacteristic *rotationSpeed;       // 可選：風速
  SpanCharacteristic *swingMode;           // 可選：擺動
  
  // UART相關常量
  static const uint32_t UART_TIMEOUT_MS = 500;    // UART超時時間
  static const uint32_t COMMAND_DELAY_MS = 100;   // 命令間延遲
  static const uint32_t RETRY_MAX = 3;            // 最大重試次數
  static const uint32_t BUFFER_THRESHOLD = 200;   // 緩衝區閾值
  
  // UART狀態
  struct {
    bool initialized;
    uint32_t lastError;
    uint32_t errorCount;
    uint32_t lastInitTime;    // 上次初始化時間
    uint32_t lastResetTime;   // 上次重置時間
  } uart_state;

  S21AC() : Service::HeaterCooler() {
    // 初始化必需的特性
    active = new Characteristic::Active(0);                                    // 必需：開關狀態
    currentTemp = new Characteristic::CurrentTemperature(20);                  // 必需：當前溫度
    currentMode = new Characteristic::CurrentHeaterCoolerState(0);            // 必需：當前模
    targetMode = new Characteristic::TargetHeaterCoolerState(0);              // 必需：目標模式
    
    // 初始化可選的特性
    targetTemp = new Characteristic::CoolingThresholdTemperature(23);         // 可選：製冷目標溫度
    heatingTemp = new Characteristic::HeatingThresholdTemperature(23);        // 可選：製熱目標溫度
    rotationSpeed = new Characteristic::RotationSpeed(0);                     // 可選：風速
    swingMode = new Characteristic::SwingMode(0);                            // 可選：擺動
    
    // 設置有效範圍
    targetTemp->setRange(18, 30, 0.5);    // 冷氣溫度範圍
    heatingTemp->setRange(18, 30, 0.5);   // 暖氣溫度範圍
    rotationSpeed->setRange(0, 100, 20);  // 5 段風速
    currentTemp->setRange(0, 100, 0.1);   // 溫度顯示範圍
    
    // 設置有效的模式
    targetMode->setValidValues(0,3);      // 0=AUTO, 1=HEAT, 2=COOL, 3=DRY
    
    // 初始化 S21 狀態
    memset(&s21, 0, sizeof(s21));
    s21.temp = 23;
    s21.roomTemp = 20;
    
    // 初始化 UART
    uart_state = {false, ESP_OK, 0, 0, 0};
    
    // 初始化UART
    if(!initUART()) {
      WEBLOG("Failed to initialize UART during construction");
    }
  }

  bool update() override {
    bool changed = false;
    bool needVerify = false;
    static uint32_t lastModeChange = 0;
    static uint32_t lastStateChange = 0;
    
    // 臨時存儲新的狀態
    S21State new_state = {
      s21.power,
      s21.mode,
      s21.temp,
      s21.fan,
      s21.swingV
    };
    
    if(active->updated()) {
      new_state.power = active->getNewVal();
      WEBLOG("S21: Power change requested to %s", new_state.power ? "ON" : "OFF");
      changed = true;
      needVerify = true;
      lastStateChange = millis();  // 記錄狀態改變時間
    }
    
    if(targetMode->updated()) {
      new_state.power = true;
      uint8_t mode = targetMode->getNewVal();
      switch(mode) {
        case 0: // AUTO
          new_state.mode = 8;
          WEBLOG("S21: Mode change requested to AUTO");
          break;
        case 1: // HEAT
          new_state.mode = 4;
          WEBLOG("S21: Mode change requested to HEAT");
          break;
        case 2: // COOL
          new_state.mode = 3;
          WEBLOG("S21: Mode change requested to COOL");
          break;
        case 3: // DRY
          new_state.mode = 2;
          WEBLOG("S21: Mode change requested to DRY");
          break;
      }
      changed = true;
      needVerify = true;
      lastModeChange = millis();
      lastStateChange = millis();  // 同時更新狀態改變時間
    }
    
    if(targetTemp->updated() || heatingTemp->updated()) {
      float temp;
      if(new_state.mode == FAIKIN_MODE_COOL) {
        temp = targetTemp->getNewVal();
      } else if(new_state.mode == FAIKIN_MODE_HEAT) {
        temp = heatingTemp->getNewVal();
      } else {
        temp = targetTemp->getNewVal();
      }
      
      // 加範圍驗證
      if(temp >= 18.0 && temp <= 30.0) {
        new_state.temp = temp;
        WEBLOG("S21: Target temp change requested to %.1f°C", new_state.temp);
        changed = true;
        needVerify = true;
      } else {
        WEBLOG("S21: Invalid temperature value requested: %.1f°C", temp);
        return false;
      }
    }
    
    if(rotationSpeed->updated()) {
      int speed = rotationSpeed->getNewVal();
      if(speed == 0) {
        new_state.fan = FAIKIN_FAN_AUTO;
      } else if(speed <= 20) {
        new_state.fan = FAIKIN_FAN_1;
      } else if(speed <= 40) {
        new_state.fan = FAIKIN_FAN_2;
      } else if(speed <= 60) {
        new_state.fan = FAIKIN_FAN_3;
      } else if(speed <= 80) {
        new_state.fan = FAIKIN_FAN_4;
      } else {
        new_state.fan = FAIKIN_FAN_5;
      }
      WEBLOG("S21: Fan speed change requested to %d", new_state.fan);
      changed = true;
    }
    
    if(swingMode->updated()) {
      new_state.swingV = swingMode->getNewVal();
      WEBLOG("S21: Swing mode change requested to %s", new_state.swingV ? "ON" : "OFF");
      changed = true;
    }
    
    // 如果有更新就發送命令
    if(changed) {
      uint8_t data[4];
      data[0] = new_state.power ? '1' : '0';
      data[1] = new_state.mode + '0';
      data[2] = s21_encode_target_temp(new_state.temp);
      data[3] = s21_encode_fan(new_state.fan);
      
      // 發送主要命令
      if(!sendCommand('D', '1', data, 4)) {
        WEBLOG("S21: Failed to send main command");
        return false;
      }
      
      // 如果需要驗證，等待並檢查狀態
      if(needVerify) {
        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待設備處理命令
        
        if(!verifyState(new_state)) {
          WEBLOG("S21: State verification failed");
          return false;
        }
      }
      
      // 更新擺動設置
      if(new_state.swingV != s21.swingV) {
        data[0] = new_state.swingV ? '1' : '0';
        data[1] = '0';
        data[2] = '0';
        data[3] = '0';
        
        if(!sendCommand('D', '5', data, 4)) {
          WEBLOG("S21: Failed to send swing command");
          return false;
        }
      }
      
      // 更新內部狀態
      s21.power = new_state.power;
      s21.mode = new_state.mode;
      s21.temp = new_state.temp;
      s21.fan = new_state.fan;
      s21.swingV = new_state.swingV;
      
      // 更新當前模式
      if(!new_state.power) {
        currentMode->setVal(0);  // INACTIVE
      } else {
        uint8_t currentState;
        switch(new_state.mode) {
          case FAIKIN_MODE_AUTO:
            currentState = 1;  // IDLE
            break;
          case FAIKIN_MODE_HEAT:
            currentState = 2;  // HEATING
            break;
          case FAIKIN_MODE_COOL:
            currentState = 3;  // COOLING
            break;
          case FAIKIN_MODE_FAN:
          case FAIKIN_MODE_DRY:
            currentState = 1;  // 風扇和除濕模式視為 IDLE
            break;
          default:
            currentState = currentMode->getVal();
            break;
        }
        currentMode->setVal(currentState);
      }
    }
    
    return true;
  }

  // 驗證設備狀態
  bool verifyState(const S21State& expected_state) {
    static const uint32_t VERIFY_TIMEOUT = 10000;  // 10秒超時
    static const uint32_t VERIFY_INTERVAL = 1000;  // 1秒間隔
    uint32_t startTime = millis();
    uint32_t lastVerifyTime = 0;
    int successCount = 0;  // 連續成功次數
    
    while(millis() - startTime < VERIFY_TIMEOUT) {
      uint32_t now = millis();
      
      // 控制驗證間隔
      if(lastVerifyTime > 0 && now - lastVerifyTime < VERIFY_INTERVAL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      
      lastVerifyTime = now;
      
      if(!sendCommand('F', '1')) {
        WEBLOG("S21: Failed to send status query command");
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      
      uint8_t buf[32];
      int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(UART_TIMEOUT_MS));
      
      // 記錄接收到的數據
      if(len > 0) {
        char logBuf[128];
        sprintf(logBuf, "S21 RX: Data=");
        for(int i = 0; i < len; i++) {
          char hex[4];
          sprintf(hex, "%02X ", buf[i]);
          strcat(logBuf, hex);
        }
        WEBLOG(logBuf);
      }
      
      if(len >= 7 && buf[len-1] == ETX) {
        // 檢查回應格式
        if(buf[0] == 'G' && buf[1] == '1') {
          bool power = (buf[2] == '1');
          uint8_t mode = buf[3] - '0';
          float temp = s21_decode_target_temp(buf[4]);
          
          WEBLOG("S21: Status response: power=%d, mode=%d, temp=%.1f°C", 
                 power, mode, temp);
          
          // 驗證電源狀態
          if(power != expected_state.power) {
            WEBLOG("S21: Power state mismatch, expected %d, got %d", 
                   expected_state.power, power);
            successCount = 0;
            continue;
          }
          
          // 只在開機狀態下驗證其他參數
          if(power) {
            // 特殊處理模式 7
            if(mode == 7) {
              mode = FAIKIN_MODE_AUTO;  // 將模式 7 視為自動模式
              WEBLOG("S21: Converting mode 7 to AUTO mode");
            }
            
            // 驗證模式
            if(mode != expected_state.mode) {
              WEBLOG("S21: Mode mismatch, expected %d, got %d", 
                     expected_state.mode, mode);
              successCount = 0;
              continue;
            }
            
            // 驗證溫度（允許0.5度的誤差）
            if(temp < 17.0 || temp > 30.0) {
              WEBLOG("S21: Invalid temperature value: %.1f°C", temp);
              successCount = 0;
              continue;
            }
            
            if(fabs(temp - expected_state.temp) > 0.5) {
              WEBLOG("S21: Temperature mismatch, expected %.1f°C, got %.1f°C", 
                     expected_state.temp, temp);
              successCount = 0;
              continue;
            }
          }
          
          // 增加連續成功計數
          successCount++;
          
          // 需要連續兩次驗證成功
          if(successCount >= 2) {
            WEBLOG("S21: State verification successful after %d attempts", 
                   (millis() - startTime) / VERIFY_INTERVAL);
            return true;
          }
          
          continue;
        }
      }
      
      successCount = 0;  // 重置成功計數
      WEBLOG("S21: Invalid status response");
    }
    
    WEBLOG("S21: State verification failed after timeout");
    return false;
  }

  void loop() {
    static uint32_t lastUpdate = 0;
    static uint32_t lastTempUpdate = 0;
    static uint8_t failureCount = 0;
    uint32_t now = millis();
    
    // 新溫度（每 5 秒）
    if(now - lastTempUpdate >= 5000) {
      lastTempUpdate = now;
      
      if(!updateTemperature()) {
        failureCount++;
        if(failureCount > 3) {
          WEBLOG("S21: Multiple temperature update failures, attempting recovery...");
          if(resetUART()) {
            failureCount = 0;
          }
        }
      } else {
        failureCount = 0;
      }
    }
    
    // 更新狀態（每 5 秒）
    if(now - lastUpdate >= 5000) {
      lastUpdate = now;
      
      if(!updateStatus()) {
        failureCount++;
        if(failureCount > 3) {
          WEBLOG("S21: Multiple status update failures, attempting recovery...");
          if(resetUART()) {
            failureCount = 0;
          }
        }
      } else {
        failureCount = 0;
      }
    }
    
    // 增加延遲，確保命令間足夠間隔
    vTaskDelay(pdMS_TO_TICKS(COMMAND_DELAY_MS));
  }

  // UART初始化
  bool initUART() {
    uint32_t now = millis();
    
    // 檢查是否太頻繁初始化
    if(uart_state.lastInitTime > 0 && now - uart_state.lastInitTime < 5000) {
      WEBLOG("S21: UART initialization too frequent, waiting...");
      return false;
    }
    
    // 先清理之前的UART配置
    if(uart_state.initialized) {
      uart_driver_delete(UART_NUM);
      vTaskDelay(pdMS_TO_TICKS(1000));
      uart_state.initialized = false;
    }
    
    // 配置UART
    uart_config_t uart_config = {
      .baud_rate = 2400,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_EVEN,
      .stop_bits = UART_STOP_BITS_2,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
    };
    
    esp_err_t err;
    
    // 設置UART參數
    if((err = uart_param_config(UART_NUM, &uart_config)) != ESP_OK) {
      WEBLOG("UART config failed: %d", err);
      uart_state.lastError = err;
      return false;
    }
    
    // 設置UART引腳
    if((err = uart_set_pin(UART_NUM, TX_PIN, RX_PIN, -1, -1)) != ESP_OK) {
      WEBLOG("UART pin config failed: %d", err);
      uart_state.lastError = err;
      return false;
    }
    
    // 安裝UART驅動
    if((err = uart_driver_install(UART_NUM, BUF_SIZE, BUF_SIZE, 0, NULL, 0)) != ESP_OK) {
      WEBLOG("UART driver install failed: %d", err);
      uart_state.lastError = err;
      return false;
    }
    
    // 清空緩衝區
    flushUART();
    
    uart_state.initialized = true;
    uart_state.lastError = ESP_OK;
    uart_state.errorCount = 0;
    uart_state.lastInitTime = now;
    
    WEBLOG("UART initialized successfully");
    return true;
  }
  
  // UART重置
  bool resetUART() {
    uint32_t now = millis();
    static uint32_t lastHardReset = 0;  // 新增：記錄上次硬重置時間
    
    // 檢查是否太頻繁重置
    if(uart_state.lastResetTime > 0 && now - uart_state.lastResetTime < 5000) {
      WEBLOG("S21: UART reset too frequent, waiting...");
      return false;
    }
    
    WEBLOG("Resetting UART...");
    
    // 記錄錯誤
    uart_state.errorCount++;
    uart_state.lastResetTime = now;
    
    // 新增：如果錯誤持續發生，執行硬重置
    bool needHardReset = false;
    if(uart_state.errorCount > 10 && (now - lastHardReset > 300000)) {  // 5分鐘內最多一次硬重置
      WEBLOG("S21: Performing hard reset due to persistent errors");
      needHardReset = true;
      lastHardReset = now;
      uart_state.errorCount = 0;  // 重置錯誤計數
    }
    
    // 刪除UART驅動
    uart_driver_delete(UART_NUM);
    
    if(needHardReset) {
      // 執行硬重置：重置引腳
      gpio_reset_pin((gpio_num_t)TX_PIN);
      gpio_reset_pin((gpio_num_t)RX_PIN);
      vTaskDelay(pdMS_TO_TICKS(2000));  // 等待更長時間
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 重新初始化
    if(initUART()) {
      WEBLOG("UART reset successful");
      return true;
    }
    
    WEBLOG("UART reset failed");
    return false;
  }

  // 清理UART緩衝區
  void flushUART() {
    size_t buffered_size;
    uart_get_buffered_data_len(UART_NUM, &buffered_size);
    if(buffered_size > 0) {
      WEBLOG("S21: Flushing %d bytes from UART buffer", buffered_size);
    }
    uart_flush(UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // 檢查UART緩衝區狀態
  bool checkUARTBuffer() {
    size_t buffered_size;
    static uint32_t lastWarning = 0;
    uint32_t now = millis();
    
    if(uart_get_buffered_data_len(UART_NUM, &buffered_size) == ESP_OK) {
      // 新增：記錄緩衝區使用情況
      if(buffered_size > 0 && now - lastWarning > 10000) {  // 每10秒最多記錄一次
        WEBLOG("S21: UART buffer usage: %d bytes", buffered_size);
        lastWarning = now;
      }
      
      if(buffered_size >= BUFFER_THRESHOLD) {
        WEBLOG("S21: UART buffer near full (%d bytes), flushing", buffered_size);
        flushUART();
        return false;
      }
      
      // 新增：檢查緩衝區是否有殘留數據
      if(buffered_size > 0) {
        uint8_t temp_buf[32];
        size_t read_size = min(buffered_size, sizeof(temp_buf));
        int len = uart_read_bytes(UART_NUM, temp_buf, read_size, 0);
        if(len > 0) {
          WEBLOG("S21: Cleared %d bytes of residual data from buffer", len);
        }
      }
    } else {
      WEBLOG("S21: Failed to get buffer length");
      return false;
    }
    return true;
  }

  // 發送命令的改進版本
  bool sendCommand(char cmd1, char cmd2, const uint8_t *data = NULL, size_t len = 0) {
    static const uint16_t RETRY_DELAYS[] = {200, 500, 1000}; // 增加重試延遲時間
    static uint32_t lastCommandTime = 0;
    uint32_t now = millis();
    
    // 確保命令間有足夠間隔
    if(lastCommandTime > 0 && now - lastCommandTime < 100) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if(!uart_state.initialized) {
      WEBLOG("S21: UART not initialized, attempting to initialize...");
      if(!initUART()) {
        return false;
      }
    }
    
    // 檢查緩衝區狀態
    if(!checkUARTBuffer()) {
      vTaskDelay(pdMS_TO_TICKS(100));  // 等待緩衝區清理
      if(!checkUARTBuffer()) {
        return false;
      }
    }
    
    for(int retry = 0; retry < RETRY_MAX; retry++) {
      // 每次重試前清空緩衝區
      flushUART();
      vTaskDelay(pdMS_TO_TICKS(50));  // 等待緩衝區清理完成
      
      if(sendCommandOnce(cmd1, cmd2, data, len)) {
        lastCommandTime = millis();
        uart_state.errorCount = 0;  // 重置錯誤計數
        return true;
      }
      
      // 檢查UART狀態
      if(!uart_state.initialized) {
        WEBLOG("S21: UART lost initialization during retry");
        if(!initUART()) {
          break;
        }
      }
      
      // 動態調整重試延遲
      uint16_t delay = RETRY_DELAYS[retry];
      if(uart_state.errorCount > 5) {
        delay *= 2;  // 如果錯誤較多，增加延遲
      }
      
      WEBLOG("S21: Command failed, retry %d/%d (delay: %dms)", retry + 1, RETRY_MAX, delay);
      vTaskDelay(pdMS_TO_TICKS(delay));
      
      // 檢查緩衝區狀態
      if(!checkUARTBuffer()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if(!checkUARTBuffer()) {
          break;
        }
      }
    }
    
    uart_state.errorCount++;  // 增加錯誤計數
    WEBLOG("S21: Command failed after all retries (error count: %d)", uart_state.errorCount);
    
    // 如果錯誤過多，觸發UART重置
    if(uart_state.errorCount > 10) {
      WEBLOG("S21: Too many errors, resetting UART");
      resetUART();
    }
    
    return false;
  }

  // 單次發送命令的輔助函數
  bool sendCommandOnce(char cmd1, char cmd2, const uint8_t *data = NULL, size_t len = 0) {
    if(!uart_state.initialized) {
      WEBLOG("S21: UART not initialized in sendCommandOnce");
      return false;
    }
    
    uint8_t buf[32];
    size_t pos = 0;
    
    // 檢查命令長度
    size_t total_len = 4 + (data ? len : 0);  // STX + CMD1 + CMD2 + DATA + CHECKSUM + ETX
    if(total_len >= sizeof(buf)) {
      WEBLOG("S21: Command too long (%d bytes)", total_len);
      return false;
    }
    
    // 組命令
    buf[pos++] = STX;
    buf[pos++] = cmd1;
    buf[pos++] = cmd2;
    
    if(data && len > 0) {
      memcpy(buf + pos, data, len);
      pos += len;
    }
    
    // 計算校驗和（不包括STX）
    uint8_t sum = 0;
    for (int i = 1; i < pos; i++) {
        sum += buf[i];
    }
    // 特殊字節處理
    if (sum == STX || sum == ETX || sum == ACK) {
        sum += 2;
    }
    buf[pos++] = sum;
    buf[pos++] = ETX;
    
    // 記錄發送的命令
    char logBuf[128];
    sprintf(logBuf, "S21 TX: CMD=%c%c, Data=", cmd1, cmd2);
    for(size_t i = 0; i < pos; i++) {
      char hex[4];
      sprintf(hex, "%02X ", buf[i]);
      strcat(logBuf, hex);
    }
    WEBLOG(logBuf);
    
    // 檢查緩衝區狀態
    if(!checkUARTBuffer()) {
      return false;
    }
    
    // 發送命令
    int written = uart_write_bytes(UART_NUM, buf, pos);
    if(written != pos) {
      WEBLOG("UART write failed: %d/%d bytes written", written, pos);
      return false;
    }
    
    // 等待 ACK
    uint8_t resp;
    int ret = uart_read_bytes(UART_NUM, &resp, 1, pdMS_TO_TICKS(UART_TIMEOUT_MS));
    
    if(ret == 1) {
      if(resp == ACK) {
        WEBLOG("S21: Received ACK");
        
        // 如果是查詢命令，等待回應數據
        if(cmd1 == 'F') {
          ret = uart_read_bytes(UART_NUM, &resp, 1, pdMS_TO_TICKS(UART_TIMEOUT_MS));
          if(ret != 1 || (resp != STX && resp != 'G')) {
            WEBLOG("S21: Invalid or no response start: 0x%02X", resp);
            return false;
          }
          
          // 發送 ACK
          resp = ACK;
          if(uart_write_bytes(UART_NUM, &resp, 1) != 1) {
            WEBLOG("S21: Failed to send ACK");
            return false;
          }
          WEBLOG("S21: Sent ACK");
          return true;
        }
        return true;
      }
      WEBLOG("S21: Received unexpected response: 0x%02X", resp);
    } else if(ret == 0) {
      WEBLOG("S21: Response timeout");
    } else {
      WEBLOG("S21: Read error: %d", ret);
      uart_state.initialized = false;  // 標記UART要重新初始化
    }
    return false;
  }

  // 更新狀態的輔助函數
  bool updateStatus() {
    if(!sendCommand('F', '1')) {
      return false;
    }
    
    uint8_t buf[32];
    int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(UART_TIMEOUT_MS));
    
    // 記錄接收到的數據
    if(len > 0) {
      char logBuf[128];
      sprintf(logBuf, "S21 RX: Data=");
      for(int i = 0; i < len; i++) {
        char hex[4];
        sprintf(hex, "%02X ", buf[i]);
        strcat(logBuf, hex);
      }
      WEBLOG(logBuf);
    }
    
    if(len >= 7 && buf[len-1] == ETX) {
      // 檢查回應格式
      if(buf[0] == 'G' && buf[1] == '1') {
        bool power = (buf[2] == '1');
        uint8_t rawMode = buf[3] - '0';
        uint8_t tempRaw = buf[4];
        
        // 記錄原始數據
        WEBLOG("S21: Raw status: power=%c, mode=%c, temp=0x%02X (%.1f°C)", 
               buf[2], buf[3], buf[4], s21_decode_target_temp(buf[4]));
        
        // 檢查電源狀態格式
        if(buf[2] != '0' && buf[2] != '1') {
          WEBLOG("S21: Invalid power value: 0x%02X", buf[2]);
          return false;
        }
        
        // 更新模式映射
        uint8_t mode;
        switch(rawMode) {
          case 0:  // 風扇模式
            mode = FAIKIN_MODE_FAN;
            break;
          case 1:  // 製熱
            mode = FAIKIN_MODE_HEAT;
            break;
          case 2:  // 除濕
            mode = FAIKIN_MODE_DRY;
            break;
          case 3:  // 製冷
            mode = FAIKIN_MODE_COOL;
            break;
          case 4:  // 製熱
            mode = FAIKIN_MODE_HEAT;
            break;
          case 7:  // 特殊模式，暫時視為自動
            mode = FAIKIN_MODE_AUTO;
            WEBLOG("S21: Special mode 7 detected, treating as AUTO mode");
            break;
          case 8:  // 自動模式
            mode = FAIKIN_MODE_AUTO;
            break;
          default:
            WEBLOG("S21: Invalid mode value: %d (raw: 0x%02X)", rawMode, buf[3]);
            return false;
        }
        
        // 更新目標模式映射
        uint8_t targetState;
        switch(mode) {
          case FAIKIN_MODE_AUTO:
            targetState = 0;  // AUTO
            break;
          case FAIKIN_MODE_HEAT:
            targetState = 1;  // HEAT
            break;
          case FAIKIN_MODE_COOL:
            targetState = 2;  // COOL
            break;
          case FAIKIN_MODE_FAN:
          case FAIKIN_MODE_DRY:
            targetState = 3;  // 風扇和除濕模式視為 AUTO
            break;
          default:
            targetState = targetMode->getVal();
            break;
        }
        
        // 更新當前模式映射
        uint8_t currentState;
        switch(mode) {
          case FAIKIN_MODE_AUTO:
            currentState = 1;  // IDLE
            break;
          case FAIKIN_MODE_HEAT:
            currentState = 2;  // HEATING
            break;
          case FAIKIN_MODE_COOL:
            currentState = 3;  // COOLING
            break;
          case FAIKIN_MODE_FAN:
          case FAIKIN_MODE_DRY:
            currentState = 1;  // 風扇和除濕模式視為 IDLE
            break;
          default:
            currentState = currentMode->getVal();
            break;
        }
        
        float temp;
        // 特殊處理關機狀態或特殊溫度值
        if(!power || tempRaw == 0x80) {
          temp = s21.temp;  // 保持上一次的有效溫度
          WEBLOG("S21: Using last valid temperature (%.1f°C) due to power off or special value", temp);
        } else {
          temp = s21_decode_target_temp(tempRaw);
          // 檢查溫度有效性（允許 17-30°C）
          if(temp < 17.0 || temp > 30.0) {
            WEBLOG("S21: Invalid temperature value: %.1f°C (raw: 0x%02X), using last valid temperature", temp, tempRaw);
            temp = s21.temp;  // 使用上一次的有效溫度
          }
        }
        
        // 使用臨時變數存儲狀態變化
        bool powerChanged = (power != s21.power);
        bool modeChanged = (mode != s21.mode);
        bool tempChanged = (temp != s21.temp);
        
        // 更新內部狀態
        s21.power = power;
        s21.mode = mode;
        s21.temp = temp;
        
        // 只在狀態確實改變時更新 HomeKit
        if(powerChanged) {
          active->setVal(power);
          WEBLOG("S21: Power state changed to %s", power ? "ON" : "OFF");
          
          if(!power) {
            currentMode->setVal(0);  // INACTIVE
          }
        }
        
        if(power) {
          // 更新目標模式
          if(targetMode->getVal() != targetState) {
            targetMode->setVal(targetState);
            WEBLOG("S21: Target mode changed to %d", targetState);
          }
          
          // 更新當前模式
          if(currentMode->getVal() != currentState) {
            currentMode->setVal(currentState);
            WEBLOG("S21: Current state changed to %d", currentState);
          }
          
          // 更新溫度設定
          if(tempChanged || modeChanged) {
            switch(mode) {
              case FAIKIN_MODE_COOL:
                targetTemp->setVal(temp);
                heatingTemp->setVal(23.0);  // 設���一個預設值
                break;
              case FAIKIN_MODE_HEAT:
                heatingTemp->setVal(temp);
                targetTemp->setVal(23.0);   // 設置一個預設值
                break;
              case FAIKIN_MODE_AUTO:
                targetTemp->setVal(temp);
                heatingTemp->setVal(temp);
                break;
            }
            WEBLOG("S21: Target temp updated: %.1f°C (mode: %d)", temp, mode);
          }
        }
        return true;
      } else {
        WEBLOG("S21: Invalid status response format: %02X %02X", buf[0], buf[1]);
        return false;
      }
    }
    return false;
  }

  // 更新溫度的輔助函數
  bool updateTemperature() {
    static uint32_t lastStateChange = 0;
    static uint32_t lastModeChange = 0;
    static uint8_t consecutiveFailures = 0;
    static bool stateTransition = false;
    static uint32_t lastSuccessfulUpdate = 0;  // 新增：記錄上次成功更新時間
    uint32_t now = millis();
    
    // 檢查是否在狀態轉換中
    if(stateTransition) {
      if(now - lastStateChange < 10000) {  // 等待10秒
        WEBLOG("S21: In state transition, skipping temperature query");
        return true;
      }
      stateTransition = false;
    }
    
    // 如果是關機狀態，不進行溫度查詢
    if(!s21.power) {
      consecutiveFailures = 0;
      return true;
    }
    
    // 在模式切換後等待更長時間
    if(now - lastModeChange < 8000) {  // 增加到8秒
      WEBLOG("S21: Waiting for mode change stabilization...");
      return true;
    }
    
    // 新增：檢查上次成功更新時間
    if(lastSuccessfulUpdate > 0 && now - lastSuccessfulUpdate > 30000) {  // 30秒無成功更新
      WEBLOG("S21: No successful temperature update for 30 seconds, forcing UART reset");
      resetUART();
      lastSuccessfulUpdate = now;  // 重置計時器
      return false;
    }
    
    // 如果連續失敗次數過多，增加等待時間
    if(consecutiveFailures > 3) {
      uint32_t waitTime = min(consecutiveFailures * 1000, 5000);  // 動態等待時間，最多5秒
      WEBLOG("S21: Too many consecutive failures (%d), waiting %dms...", consecutiveFailures, waitTime);
      vTaskDelay(pdMS_TO_TICKS(waitTime));
    }

    // 檢查UART緩衝區
    if(!checkUARTBuffer()) {
      WEBLOG("S21: UART buffer issue, skipping temperature query");
      return true;
    }

    // 新增：在發送命令前清空緩衝區
    flushUART();

    if(!sendCommand('F', '9')) {
      consecutiveFailures++;
      if(consecutiveFailures > 5) {
        WEBLOG("S21: Temperature query failing consistently, may need recovery");
        stateTransition = true;  // 記需要等待
        lastStateChange = now;
      }
      return false;
    }
    
    uint8_t buf[32];
    int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(UART_TIMEOUT_MS));
    
    if(len > 0) {
      char logBuf[128];
      sprintf(logBuf, "S21 RX: Data=");
      for(int i = 0; i < len; i++) {
        char hex[4];
        sprintf(hex, "%02X ", buf[i]);
        strcat(logBuf, hex);
      }
      WEBLOG(logBuf);
    }
    
    if(len >= 5 && buf[len-1] == ETX) {
      if(buf[0] == 'G' && buf[1] == '9') {
        consecutiveFailures = 0;
        lastSuccessfulUpdate = now;  // 更新成功時間戳
        
        uint8_t roomTempRaw = buf[2];
        uint8_t outdoorTempRaw = buf[3];
        
        float roomTemp, outdoorTemp;
        
        // 解碼室內溫度
        roomTemp = (roomTempRaw & 0x7F) * 0.5;
        if(!(roomTempRaw & 0x80)) {
          roomTemp = roomTemp - 18.0;
        }
        
        // 解碼室外溫度
        outdoorTemp = (outdoorTempRaw & 0x7F) * 0.5;
        if(!(outdoorTempRaw & 0x80)) {
          outdoorTemp = outdoorTemp - 18.0;
        }
        
        WEBLOG("S21: Raw temps: room=0x%02X (%.1f°C), outdoor=0x%02X (%.1f°C)", 
               roomTempRaw, roomTemp, outdoorTempRaw, outdoorTemp);
        
        bool tempChanged = false;
        
        // 檢查室內溫度是否在合理範圍內並防止突變
        if(roomTemp >= 0.0 && roomTemp <= 50.0) {
          // 新增：防止溫度突變（超過5度的變化）
          if(s21.roomTemp > 0 && fabs(roomTemp - s21.roomTemp) > 5.0) {
            WEBLOG("S21: Temperature change too large (%.1f -> %.1f), ignoring", s21.roomTemp, roomTemp);
            return true;
          }
          
          if(roomTemp != s21.roomTemp) {
            s21.roomTemp = roomTemp;
            currentTemp->setVal(roomTemp);
            WEBLOG("S21: Room temp: %.1f°C", roomTemp);
            tempChanged = true;
          }
        } else {
          WEBLOG("S21: Invalid room temp: %.1f°C (raw: 0x%02X)", roomTemp, roomTempRaw);
          return false;
        }
        
        // 檢查室外溫度是否在合理範圍內並防止突變
        if(outdoorTemp >= -20.0 && outdoorTemp <= 50.0) {
          // 新增：防止溫度突變
          if(s21.outdoorTemp != 0 && fabs(outdoorTemp - s21.outdoorTemp) > 5.0) {
            WEBLOG("S21: Outdoor temperature change too large (%.1f -> %.1f), ignoring", s21.outdoorTemp, outdoorTemp);
            return true;
          }
          
          if(outdoorTemp != s21.outdoorTemp) {
            s21.outdoorTemp = outdoorTemp;
            WEBLOG("S21: Outdoor temp: %.1f°C", outdoorTemp);
            tempChanged = true;
          }
        } else {
          WEBLOG("S21: Invalid outdoor temp: %.1f°C (raw: 0x%02X)", outdoorTemp, outdoorTempRaw);
          return false;
        }
        
        return tempChanged || true;
      } else {
        WEBLOG("S21: Invalid temperature response format: %02X %02X", buf[0], buf[1]);
        consecutiveFailures++;
        return false;
      }
    }
    consecutiveFailures++;
    return false;
  }

  // 驗證狀態數據的輔助函數
  bool validateStatus(const uint8_t *buf, size_t len) {
    if(len < 7 || buf[len-1] != ETX) {
      WEBLOG("S21: Invalid status response length or ETX");
      return false;
    }
    
    // 檢查命令格式
    if(buf[0] != STX || buf[1] != 'F' || buf[2] != '1') {
      WEBLOG("S21: Invalid status response format");
      return false;
    }
    
    bool power = (buf[3] == '1');
    uint8_t mode = buf[4] - '0';
    float temp = s21_decode_target_temp(buf[5]);
    
    // 記錄原始數據
    WEBLOG("S21: Raw status: power=%c, mode=%c, temp=0x%02X (%.1f°C)", 
           buf[3], buf[4], buf[5], temp);
    
    // 檢查電源狀態格式
    if(buf[3] != '0' && buf[3] != '1') {
      WEBLOG("S21: Invalid power value: 0x%02X", buf[3]);
      return false;
    }
    
    // 檢查模式有效性
    if(mode > FAIKIN_MODE_DRY) {
      WEBLOG("S21: Invalid mode value: %d (raw: 0x%02X)", mode, buf[4]);
      return false;
    }
    
    // 檢查溫度有效性
    if(temp < 16.0 || temp > 30.0) {
      WEBLOG("S21: Invalid temperature value: %.1f°C (raw: 0x%02X)", temp, buf[5]);
      return false;
    }
    
    return true;
  }
};

// WiFi 狀態回調函數
void wifiCallback() {
  if(WiFi.status() == WL_CONNECTED) {
    WEBLOG("Connected to WiFi!");
    WEBLOG("IP Address: %s", WiFi.localIP().toString().c_str());
  } else if(WiFi.status() == WL_CONNECT_FAILED) {
    WEBLOG("Failed to connect to WiFi. Starting AP Mode...");
  } else {
    WEBLOG("Connecting to WiFi...");
  }
}

void setup() {
  Serial.begin(115200);
  
  // 啟用詳細的 weblog
  homeSpan.enableWebLog(500, "status");      // 加日誌條數到 500
  homeSpan.setLogLevel(2);                   // 設置最詳細的日誌級別
  
  // 啟用 UART 試輸出
  WEBLOG("Starting S21 debug mode...");
  
  // WiFi 設置
  homeSpan.setWifiCredentials("suzuke", "0978362789");
  
  // AP 式設置
  homeSpan.setApSSID("suzuke");
  homeSpan.setApPassword("0978362789");
  homeSpan.setApTimeout(300);
  homeSpan.enableAutoStartAP();
  
  // HomeKit 設置
  homeSpan.setPairingCode("11122333");
  homeSpan.setStatusPin(2);
  homeSpan.setHostNameSuffix("AC");
  homeSpan.setWifiCallback(wifiCallback);

  homeSpan.begin(Category::Thermostats, "Daikin AC");
  
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("Daikin AC");
      new Characteristic::Manufacturer("Daikin");
      new Characteristic::Model("S21");
      new Characteristic::SerialNumber("123-ABC");
      new Characteristic::FirmwareRevision("1.0");
    
    new S21AC();
}

void loop() {
  homeSpan.poll();
}