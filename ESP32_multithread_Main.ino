#include <time.h>
#include <WiFi.h>
#include <OneWire.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PZEM004Tv30.h>
#include <FirebaseESP32.h>
#include <HardwareSerial.h>
#include <DallasTemperature.h>

// Thông tin Firebase
#define FIREBASE_HOST "https://todolistapp-408f2-default-rtdb.asia-southeast1.firebasedatabase.app/" // đường dẫn đến firebase database
#define FIREBASE_AUTH "iej75ZGaEnpVDwfR67qv82qSa7GNq60DJf9AO5ig" KEY AUTHENTICATION Của bạn

#define WIFI_SSID "KLTN_DT4"
#define WIFI_PASSWORD "123455555"

#define SHUTDOWN_TIMEOUT 600000

// Khai báo IP của các máy con
const char* serverIPs[] = {
  "192.168.176.2",
  "192.168.1.78",
  "192.168.142.118",
  "192.168.1.104"
};
const int serverPort = 8080;

// cảm biến HLK-LD2410B-P
#define OUT_PIN 15  //HLK-LD2410B-P
HardwareSerial hlkSerial(1);

// Cảm biến DS18B20
#define OUT_TEM_1 13  // Pin for the DS18B20 c1 la 13
#define OUT_TEM_2 33  // Pin for the DS18B20 c2 la 33
#define OUT_TEM_3 14  // Pin for the DS18B20 c3 la 14
#define OUT_TEM_4 32  // Pin for the DS18B20 c4 la 27
OneWire oneWire1(OUT_TEM_1);
OneWire oneWire2(OUT_TEM_2);
OneWire oneWire3(OUT_TEM_3);
OneWire oneWire4(OUT_TEM_4);
DallasTemperature sensors1(&oneWire1);
DallasTemperature sensors2(&oneWire2);
DallasTemperature sensors3(&oneWire3);
DallasTemperature sensors4(&oneWire4);

// Cảm biến PZEM004T
#define RXD2 16  // UART FOR PZEM
#define TXD2 17  // UART FOR PZEM
HardwareSerial pzemSerial(2);
PZEM004Tv30 pzem(pzemSerial, RXD2, TXD2);

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

Preferences preferencesBootCount;
Preferences preferencesOnCount;

// Relay
#define RELAY_1_PIN 5   // GPIO5 cho relay 1 (máy tính 1)
#define RELAY_2_PIN 18  // GPIO18 cho relay 2 (máy tính 2)
#define RELAY_3_PIN 19  // GPIO19 cho relay 3 (máy tính 3)
#define RELAY_4_PIN 21  // GPIO21 cho relay 4 (máy tính 4)

bool isAutoMode = false;
bool manualToAutoSwitch = false;

bool relayStates[4] = { false, false, false, false };
int relayOnCounts[4] = { 0, 0, 0, 0 };  // Số lần bật của từng relay
int relays[] = { RELAY_1_PIN, RELAY_2_PIN, RELAY_3_PIN, RELAY_4_PIN };

// Task handles
TaskHandle_t firebaseTaskHandle;
TaskHandle_t relayTaskHandle;
TaskHandle_t temperaturesTaskHandle;

// semaphore
SemaphoreHandle_t relayMutex;

// Use only core 1
#if CONFIG_FREERTOS_UNICORE
static const BaseType_t app_cpu = 0;
#else
static const BaseType_t app_cpu = 1;
#endif

void setup() {
  Serial.begin(115200);                            // giao tiếp với serial monitor
  pzemSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);  //Pzem
  hlkSerial.begin(256000, SERIAL_8N1);             //HLK
  pinMode(OUT_PIN, INPUT_PULLDOWN);                //HLK
  pinMode(RELAY_1_PIN, OUTPUT);                    // Cấu hình relay là OUTPUT
  pinMode(RELAY_2_PIN, OUTPUT);
  pinMode(RELAY_3_PIN, OUTPUT);
  pinMode(RELAY_4_PIN, OUTPUT);
  // Thiết lập các chân relay là OUTPUT
  for (int i = 0; i < 4; i++) {
    pinMode(relays[i], OUTPUT);
    digitalWrite(relays[i], HIGH);  // Đặt mặc định tất cả relay ở mức HIGH (tắt relay)
  }
  sensors1.begin();
  sensors2.begin();
  sensors3.begin();
  sensors4.begin();

  //Khởi tạo Wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print("...");
  }
  Serial.println("\nConnected to WiFi");

  setupTime();                            //gọi hàm khởi tạo thời gian
  String currentTime = getCurrentTime();  //tạo biến lấy thời gian hiện tại
  Serial.println(currentTime);

  // firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  // Thiết lập thời gian chờ cho firebase
  config.timeout.serverResponse = 15000;    // 10 giây
  config.timeout.socketConnection = 15000;  // 10 giây

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Lấy trạng thái relayOnCounts
  downloadRelayCountFirebase();

  //Khởi tạo semaphore mutex
  relayMutex = xSemaphoreCreateMutex();  // Khởi tạo mutex
  if (relayMutex == NULL) {
    Serial.println("Failed to create mutex");
  }

  xTaskCreatePinnedToCore(     // Use xTaskCreate() in vanilla FreeRTOS
    TaskControlPC,             // Function to be called
    "TaskControlPC",           // Name of task
    34192,                     // Stack size (bytes in ESP32, words in FreeRTOS)
    NULL,                      // Parameter to pass to function
    configMAX_PRIORITIES - 1,  // Task priority (0 to to configMAX_PRIORITIES - 1)
    &relayTaskHandle,          // Task handle
    app_cpu);

  xTaskCreatePinnedToCore(   // Use xTaskCreate() in vanilla FreeRTOS
    TaskUploadAllSensors,    // Function to be called
    "TaskUploadAllSensors",  // Name of task
    24192,                   // Stack size (bytes in ESP32, words in FreeRTOS)
    NULL,                    // Parameter to pass to function
    1,                       // Task priority (0 to to configMAX_PRIORITIES - 1)
    &relayTaskHandle,        // Task handle
    app_cpu);                // Run on one core for demo purposes (ESP32 only)

  xTaskCreatePinnedToCore(            // Use xTaskCreate() in vanilla FreeRTOS
    TaskUploadTemperatureAfter30M,    // Function to be called
    "TaskUploadTemperatureAfter30M",  // Name of task
    12192,                            // Stack size (bytes in ESP32, words in FreeRTOS)
    NULL,                             // Parameter to pass to function
    1,                                // Task priority (0 to to configMAX_PRIORITIES - 1)
    &temperaturesTaskHandle,          // Task handle
    app_cpu);                         // Run on one core for demo purposes (ESP32 only)
}

// 1.0 Hàm khởi tạo thời gian
bool setupTime() {
  // Thiết lập múi giờ Việt Nam (GMT+7)
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  // Lấy thời gian từ NTP server
  if (!getLocalTime(&timeinfo)) {
    // Serial.println("1.0 Lỗi: Không thể lấy thời gian!");
    return false;  // Trả về false nếu thất bại
  }
  // In thời gian hiện tại nếu thành công
  // Serial.print("1.0 Lấy thời gian thành công: ");
  Serial.println(&timeinfo, "Current time: %Y-%m-%d_%H:%M:%S");
  return true;  // Trả về true nếu thành công
}

// 1.1 Hàm lấy thời gian định dạng: năm - tháng - ngày - thời gian
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // return "1.1 Lỗi không lấy được thời gian ở hàm getCurrentTime";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H:%M:%SS", &timeinfo);  // Định dạng thời gian
  return String(timeString);
}

// 1.2 Hàm lấy thời gian định dạng: năm - tháng - ngày
String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // return "1.2 Lỗi không lấy được thời gian ở hàm getCurrentDate";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d", &timeinfo);  // Định dạng thời gian
  return String(timeString);
}

// 1.3 Hàm get số lần bật tắt máy ban đầu
void downloadRelayCountFirebase() {
  String today = getCurrentDate();
  if (today == "") {
    // Serial.println("1.3 Failed to get current date!");
    return;
  }
  String path = "/ComputerOnCount/" + today;
  if (Firebase.get(firebaseData, path)) {
    if (firebaseData.dataType() == "json") {
      FirebaseJson& json = firebaseData.jsonObject();
      FirebaseJsonData jsonData;
      // Duyệt qua từng phần tử để cập nhật relayOnCounts
      for (int i = 0; i < 4; i++) {
        String key = "computer" + String(i + 1) + "/onCount";
        if (json.get(jsonData, key)) {
          relayOnCounts[i] = jsonData.intValue;
        } else {
          Serial.printf("1.3 Failed to get %s\n", key.c_str());
        }
      }
      // In kết quả ra Serial để kiểm tra
      Serial.print("1.3 Updated relayOnCounts: ");
      for (int i = 0; i < 4; i++) {
        Serial.print(relayOnCounts[i]);
        Serial.print(" ");
      }
    } else {
      Serial.println("1.3 Data type mismatch or no data!");
    }
  } else {
    Serial.printf("1.3 Failed to get data from Firebase: %s\n", firebaseData.errorReason().c_str());
  }
}

// 1.4 Hàm get trạng thái auto trên firebase
bool getAuto() {
  checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi
  if (Firebase.getBool(firebaseData, "/isAuto/status")) {
    return firebaseData.boolData();
  } else {
    // Serial.printf("1.4 Failed to get auto mode: %s\n", firebaseData.errorReason().c_str());
    return false;
  }
}

// 1.5 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
void controlComputersForRadar() {
  static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người
  TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (đơn vị tick)
  int radarState = digitalRead(OUT_PIN);         // Đọc trạng thái radar

  // Chuyển đổi timeout sang tick (nếu chưa chuyển trước đó)
  const TickType_t shutdownTimeoutInTicks = pdMS_TO_TICKS(SHUTDOWN_TIMEOUT);

  // Kiểm tra trạng thái radar và xử lý
  if (radarState == HIGH) {      // Nếu có người
    if (isAutoMode) {            // Chỉ xử lý nếu đang ở chế độ auto
      if (manualToAutoSwitch) {  // Chuyển từ chế độ manual sang auto
        // Kiểm tra xem tất cả các relay có đang tắt không
        bool allRelaysOff = true;
        for (int i = 0; i < 4; i++) {
          if (relayStates[i]) {
            allRelaysOff = false;
            break;
          }
        }
        if (allRelaysOff) {
          Serial.println("1.5 Hệ thống đang ở chế độ auto, bật lại hai máy tính ngẫu nhiên.");
          OnRelay();  // Hàm bật ngẫu nhiên hai máy tính
        } else {
          Serial.println("1.5 Có máy tính đang bật, không cần thực hiện bật lại.");
        }
        manualToAutoSwitch = false;  // Cập nhật trạng thái chuyển đổi
      }
    }
    lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
  } else if (radarState == LOW && isAutoMode) {
    if ((currentTime - lastDetectionTime) >= shutdownTimeoutInTicks) {
      Serial.println("1.5 Không phát hiện người trong thời gian yêu cầu, tắt tất cả máy tính.");
      for (int i = 0; i < 4; i++) {
        if (relayStates[i]) {
          shutdownServer(serverIPs[i], i);
          vTaskDelay(pdMS_TO_TICKS(1000));
          digitalWrite(relays[i], HIGH);
          relayStates[i] = false;
        }
      }
    }
  }

  // TH1 + TH2: Xử lý trong chế độ Auto
  if (isAutoMode) {
    if (radarState == HIGH) {  // Nếu có người
      if ((currentTime - lastDetectionTime) >= shutdownTimeoutInTicks) {
        Serial.println("1.5 Có người trở lại: Bật 2 máy tính ngẫu nhiên.");
        OnRelay();  // Bật 2 máy tính ngẫu nhiên
      }

      bool allRelaysOff = true;
      for (int i = 0; i < 4; i++) {
        if (relayStates[i]) {  // Nếu có relay nào bật
          allRelaysOff = false;
          break;
        }
      }

      if (allRelaysOff) {
        Serial.println("1.5 Có người, nhưng không có máy nào bật. Bật 2 máy tính ngẫu nhiên.");
        OnRelay();  // Bật 2 máy tính ngẫu nhiên
      }

      lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
    } else if (radarState == LOW) {     // Nếu không có người
      if ((currentTime - lastDetectionTime) >= shutdownTimeoutInTicks) {
        Serial.println("1.5 Không phát hiện người trong 15 phút, tắt tất cả máy tính.");
        for (int i = 0; i < 4; i++) {
          if (relayStates[i]) {               // Chỉ tắt máy nếu đang bật
            shutdownServer(serverIPs[i], i);  // Gửi lệnh shutdown
            vTaskDelay(pdMS_TO_TICKS(1000));  // Chờ shutdown hoàn tất
            digitalWrite(relays[i], HIGH);    // Ngắt relay
            relayStates[i] = false;           // Cập nhật trạng thái relay
          }
        }
      }
    }
  }

  // Cập nhật trạng thái lên Firebase
  static bool lastRelayStates[4] = { false, false, false, false };
  bool relayChanged = false;
  for (int i = 0; i < 4; i++) {
    if (relayStates[i] != lastRelayStates[i]) {
      relayChanged = true;
      lastRelayStates[i] = relayStates[i];
    }
  }

  if (relayChanged) {
    Serial.println("1.10 Relay state changed, updating Firebase...");
    updateRelayStatesToFirebase(relayStates);
    updateRelayOnCountsToFirebase(relayOnCounts);
  } else {
    Serial.println("1.10 No relay state change, skipping Firebase update.");
  }

  Serial.printf("1.5 Điều khiển máy tính bởi radar: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
  updateRelayStatesToFirebase(relayStates);
}

// 1.6 Hàm điều khiển relay theo lệnh từ Firebase ở chế độ Manual version2
void controlComputersManually() {
  checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi
  for (int i = 0; i < 4; i++) {
    String relayPath = "/Computer/computer" + String(i + 1) + "/status";

    // Đọc trạng thái relay từ Firebase (thêm kiểm tra retry để ổn định hơn)
    bool success = Firebase.getInt(firebaseData, relayPath);
    int relayState = success ? firebaseData.intData() : -1;

    if (!success) {
      // Serial.printf("1.6 Không đọc được trạng thái relay %d từ Firebase. Lý do: %s\n", i + 1, firebaseData.errorReason().c_str());
      checkWiFiConnection();  // Kiểm tra lại Wi-Fi nếu thất bại
      continue;               // Bỏ qua relay này
    }
    // Kiểm tra và thay đổi trạng thái relay nếu cần
    if (relayState == 1 && !relayStates[i]) {
      Serial.printf("1.6 Bật relay %d thủ công.\n", i + 1);
      digitalWrite(relays[i], LOW);  // Kích relay
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      digitalWrite(relays[i], HIGH);  // Ngắt relay
      relayStates[i] = true;          // Cập nhật trạng thái relay
      relayOnCounts[i]++;             // Tăng số lần bật relay
      updateRelayOnCountsToFirebase(relayOnCounts);
    } else if (relayState == 0 && relayStates[i]) {
      Serial.printf("1.6 Tắt relay %d thủ công.\n", i + 1);
      shutdownServer(serverIPs[i], i);  // Gửi lệnh shutdown
      vTaskDelay(2000 / portTICK_PERIOD_MS);
      digitalWrite(relays[i], HIGH);  // Ngắt relay
      relayStates[i] = false;         // Cập nhật trạng thái relay
    }
  }
  Serial.printf("1.6 Trạng thái relay sau xử lý: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
}

// 1.7 Mảng chứa tổ hợp relay đã được định nghĩa trước 12 34 23 14 13 24
const int predefinedCombinations[6][2] = {
  { 0, 1 },  // Relay 1 & Relay 2
  { 2, 3 },  // Relay 3 & Relay 4
  { 1, 2 },  // Relay 2 & Relay 3
  { 0, 3 },  // Relay 1 & Relay 4
  { 0, 2 },  // Relay 1 & Relay 3
  { 1, 3 }   // Relay 2 & Relay 4
};
const int totalCombinations = 6;  // Tổng số tổ hợp

// 1.8 Hàm bật ngẫu nhiên 2 máy tính
void OnRelay() {
  // Mở bộ nhớ Preferences để đọc lại giá trị trước đó khi relay tắt điện
  preferencesBootCount.begin("relay-app", false);               // Mở "relay-app" trong bộ nhớ, chế độ read/write
  int bootCount = preferencesBootCount.getInt("bootCount", 0);  // Đọc giá trị "bootCount" từ Preferences (mặc định 0)
  // Serial.printf("1.8 Boot count: %d\n", bootCount);
  // Gọi hàm bật relay theo số lần khởi động hiện tại (bật 2 relay theo tổ hợp)
  turnOnRelaysByCombination(bootCount % totalCombinations);  // Giới hạn tới totalCombinations
  bootCount++;                                               // Tăng số lần khởi động cho lần tiếp theo
  if (bootCount > 1000) {
    bootCount = 0;
  }
  preferencesBootCount.putInt("bootCount", bootCount);  // Lưu giá trị mới của "bootCount" vào Preferences
  preferencesBootCount.end();                           // Đóng Preferences
}

// 1.9v2 Hàm bật relay theo cặp giảm thiểu bộ nhớ
void turnOnRelaysByCombination(int bootCount) {
  if (bootCount < 0 || bootCount >= totalCombinations) {  // Kiểm tra bootCount hợp lệ
    Serial.println("1.9 Invalid boot count");
    return;
  }

  int relay1 = predefinedCombinations[bootCount][0];
  int relay2 = predefinedCombinations[bootCount][1];

  // Bật relay1
  if (!relayStates[relay1]) {
    digitalWrite(relays[relay1], LOW);
    relayOnCounts[relay1]++;
    Serial.printf("1.9 Turned on relay %d\n", relay1 + 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(relays[relay1], HIGH);
    relayStates[relay1] = true;
  }

  // Bật relay2
  if (!relayStates[relay2]) {
    digitalWrite(relays[relay2], LOW);
    relayOnCounts[relay2]++;
    Serial.printf("1.9 Turned on relay %d\n", relay2 + 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(relays[relay2], HIGH);
    relayStates[relay2] = true;
  }
  // Cập nhật trạng thái và số lần bật tắt lên Firebase
  updateRelayStatesToFirebase(relayStates);
  updateRelayOnCountsToFirebase(relayOnCounts);
}

// 1.10v2 Hàm update trạng thái relay lên Firebase bằng JSON
void updateRelayStatesToFirebase(bool status[]) {
  checkWiFiConnection();
  // Tạo đối tượng JSON
  FirebaseJson relayStateJson;
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để thêm dữ liệu vào JSON
    String relayKey = "computer" + String(i + 1) + "/status";
    relayStateJson.set(relayKey.c_str(), status[i] ? 1 : 0);
  }
  // Đường dẫn tổng thể
  String relayPath = "/Computer";
  // Gửi toàn bộ trạng thái relay lên Firebase
  if (Firebase.set(firebaseData, relayPath, relayStateJson)) {
    // Serial.println("1.10 All relay states updated successfully.");
  } else {
    Serial.printf("1.10 Failed to update relay states: %s\n", firebaseData.errorReason().c_str());
  }
}

// 1.11v2 Hàm update số lần bật tắt máy lên Firebase bằng JSON
void updateRelayOnCountsToFirebase(int counts[]) {
  checkWiFiConnection();
  String currentTime = getCurrentDate();
  // Tạo đối tượng JSON
  FirebaseJson relayCountJson;
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để thêm dữ liệu vào JSON
    String relayKey = "computer" + String(i + 1) + "/onCount";
    relayCountJson.set(relayKey.c_str(), counts[i]);
  }
  // Đường dẫn tổng thể cho ngày hiện tại
  String relayCPath = "/ComputerOnCount/" + String(currentTime);
  // Gửi toàn bộ dữ liệu số lần bật tắt lên Firebase
  if (Firebase.set(firebaseData, relayCPath, relayCountJson)) {
    // Serial.println("1.11 All relay onCounts updated successfully.");
  } else {
    Serial.printf("1.11 Failed to update relay onCounts: %s\n", firebaseData.errorReason().c_str());
  }
}

// 1.12 Hàm kiểm tra kết nối Wi-Fi và tự động kết nối lại nếu cần
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    // Serial.println("1.12 Wi-Fi disconnected, attempting to reconnect...");
    WiFi.reconnect();  // Gọi lại Wi-Fi nếu bị mất kết nối
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    // Serial.println("1.12 Wi-Fi reconnected! OK");
  }
}

// 1.13 Hàm điều khiển Relay
void TaskControlPC(void* pvParameters) {
  for (;;) {
    checkWiFiConnection();
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      // Serial.print("1.13 Free heap: ");
      // Serial.println(esp_get_free_heap_size());
      // Serial.print("1.13 relay core: ");
      // Serial.println(xPortGetCoreID());
      handleRadarData();  // Gửi trạng thái radar
      vTaskDelay(500 / portTICK_PERIOD_MS);

      // Kiểm tra xem có ở chế độ tự động hay không
      if (getAuto()) {
        Serial.println("1.13 Đang ở chế độ auto");
        //Cập nhật trạng thái cho đúng về isAuto
        if (!isAutoMode) {
          isAutoMode = true;          // Chuyển sang chế độ auto
          manualToAutoSwitch = true;  // Đánh dấu vừa chuyển từ manual sang auto
        }
        Serial.print("1.13 Điều khiển máy tính theo cảm biển radar ");
        controlComputersForRadar();  // Điều khiển relay theo radar
      } else {
        isAutoMode = false;
        Serial.print("1.13 Đang ở chế độ thủ công");
        controlComputersManually();  // Điều khiển relay thủ công
      }
      Serial.println("1.13----------------TaskControlPC: Accessing relay.");
      xSemaphoreGive(relayMutex);
    } else {
      Serial.println("1.13 Failed to acquire relayMutex in task TaskControlPC");
      // Serial.println("1.13 Restarting Firebase...");
      Firebase.begin(&config, &auth);  // Tái khởi tạo Firebase
      Firebase.reconnectWiFi(true);    // Tự động kết nối lại Wi-Fi
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

// 1.14 Hàm upload nhiệt độ mỗi 30phuts
void TaskUploadTemperatureAfter30M(void* pvParameters) {
  for (;;) {
    checkWiFiConnection();
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      sendTemperatureAfter30Minutes();
      Serial.println("1.14----------------TaskUploadTemperatureAfter30M: Accessing relay.");
      xSemaphoreGive(relayMutex);
    } else {
      Serial.println("1.14 Failed to acquire relayMutex in task TaskUploadTemperatureAfter30M");
      // Serial.println("1.14 Restarting Firebase...");
      Firebase.begin(&config, &auth);  // Tái khởi tạo Firebase
      Firebase.reconnectWiFi(true);    // Tự động kết nối lại Wi-Fi
    }
    vTaskDelay(1800000 / portTICK_PERIOD_MS);
  }
}

void loop() {
}
