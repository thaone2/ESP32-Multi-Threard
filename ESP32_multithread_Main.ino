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
#define FIREBASE_HOST "https://todolistapp-408f2-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "iej75ZGaEnpVDwfR67qv82qSa7GNq60DJf9AO5ig"

// #define WIFI_SSID "Camera1"
#define WIFI_SSID "Ngoc Van"
#define WIFI_PASSWORD "22052020"

// #define SHUTDOWN_TIMEOUT (900000 / portTICK_PERIOD_MS)  // 15 phút (đơn vị ticks)
#define SHUTDOWN_TIMEOUT (29000 / portTICK_PERIOD_MS)  // 15 phút (đơn vị ticks)

const char* serverIPs[] = {
  "192.168.1.19",
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
#define OUT_TEM_2 13  // Pin for the DS18B20 c2 la 12 Chân 12 bị lỗi nên đổi sang chân 33
#define OUT_TEM_3 13  // Pin for the DS18B20 c3 la 14
#define OUT_TEM_4 13  // Pin for the DS18B20 c4 la 27
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
#define RELAY_1_PIN 5   // GPIO18 cho relay 1 (máy tính 1)
#define RELAY_2_PIN 18  // GPIO19 cho relay 2 (máy tính 2)
#define RELAY_3_PIN 19  // GPIO21 cho relay 3 (máy tính 3)
#define RELAY_4_PIN 21  // GPIO22 cho relay 4 (máy tính 4)

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
  // pinMode(OUT_PIN, INPUT);
  pinMode(OUT_PIN, INPUT_PULLDOWN);  //HLK
  pinMode(RELAY_1_PIN, OUTPUT);      // Cấu hình relay là OUTPUT
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
  config.timeout.serverResponse = 15000;    // 15 giây
  config.timeout.socketConnection = 15000;  // 15 giây
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Lấy trạng thái relayOnCounts 
  downloadRelayCountFirebase();

  //Khởi tạo semaphore mutex
  relayMutex = xSemaphoreCreateMutex();  // Khởi tạo mutex
  if (relayMutex == NULL) {
    Serial.println("Failed to create mutex");
  }

  // Task to run forever
  xTaskCreatePinnedToCore(     // Use xTaskCreate() in vanilla FreeRTOS
    TaskControlRelayMode,      // Function to be called
    "TaskControlRelayMode",    // Name of task
    54192,                     // Stack size (bytes in ESP32, words in FreeRTOS)
    NULL,                      // Parameter to pass to function
    configMAX_PRIORITIES - 1,  // Task priority (0 to to configMAX_PRIORITIES - 1)
    &relayTaskHandle,          // Task handle
    app_cpu);                  // Run on one core for demo purposes (ESP32 only)

  xTaskCreatePinnedToCore(  // Use xTaskCreate() in vanilla FreeRTOS
    TaskUploadHLK,          // Function to be called
    "TaskUploadHLK",        // Name of task
    34192,                  // Stack size (bytes in ESP32, words in FreeRTOS)
    NULL,                   // Parameter to pass to function
    1,                      // Task priority (0 to to configMAX_PRIORITIES - 1)
    &relayTaskHandle,       // Task handle
    app_cpu);               // Run on one core for demo purposes (ESP32 only)

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
    Serial.println("1.0 Lỗi: Không thể lấy thời gian!");
    return false;  // Trả về false nếu thất bại
  }
  // In thời gian hiện tại nếu thành công
  Serial.print("1.0 Lấy thời gian thành công: ");
  Serial.println(&timeinfo, "Current time: %Y-%m-%d_%H:%M:%S");
  return true;  // Trả về true nếu thành công
}

// 1.1 Hàm lấy thời gian định dạng: năm - tháng - ngày - thời gian
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1.1 Lỗi không lấy được thời gian ở hàm getCurrentTime";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H:%M:%SS", &timeinfo);  // Định dạng thời gian
  return String(timeString);
}

// 1.2 Hàm lấy thời gian định dạng: năm - tháng - ngày
String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1.2 Lỗi không lấy được thời gian ở hàm getCurrentDate";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d", &timeinfo);  // Định dạng thời gian
  return String(timeString);
}

// 1.3 Hàm get số lần bật tắt máy ban đầu
void downloadRelayCountFirebase() {
  String today = getCurrentDate();
  if (today == "") {
    Serial.println("1.3 Failed to get current date!");
    return;
  }
  String path = "/ComputerOnCount/" + today;
  if (Firebase.get(firebaseData, path)) {
    if (firebaseData.dataType() == "json") {
      FirebaseJson &json = firebaseData.jsonObject();
      FirebaseJsonData jsonData;
      // Duyệt qua từng phần tử để cập nhật relayOnCounts
      for (int i = 0; i < 4; i++) {
        String key = "computer" + String(i + 1) + "/onCount";
        if (json.get(jsonData, key)) {
          relayOnCounts[i] = jsonData.intValue;
        } else {
          Serial.printf("Failed to get %s\n", key.c_str());
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
    Serial.printf("1.4 Failed to get auto mode: %s\n", firebaseData.errorReason().c_str());
    return false;
  }
}

// 1.5 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
void controlComputersForRadar() {
  static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người (ticks)
  TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (ticks)
  int radarState = digitalRead(OUT_PIN);         // Đọc trạng thái radar
  if (radarState == HIGH) {                      // Nếu có người
    if (isAutoMode) {                            // Chỉ xử lý nếu đang ở chế độ auto
      if (manualToAutoSwitch) {                  // Chuyển từ chế độ manual sang auto
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
  } else {                            // Không có người
    if (isAutoMode) {                 // Chỉ xử lý nếu đang ở chế độ auto
      // Kiểm tra nếu không có người trong khoảng thời gian timeout
      if ((currentTime - lastDetectionTime) >= SHUTDOWN_TIMEOUT) {
        Serial.println("1.5 Không phát hiện người trong thời gian dài, tắt tất cả máy tính.");
        // Tắt từng máy tính và relay
        for (int i = 0; i < 4; i++) {
          if (relayStates[i]) {  // Chỉ tắt nếu relay đang bật
            shutdownServer(serverIPs[i], i);
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Chờ để đảm bảo máy tính shutdown hoàn toàn
            digitalWrite(relays[i], HIGH);          // Ngắt relay
            relayStates[i] = false;                 // Cập nhật trạng thái relay
          }
        }
      }
    }
  }
  updateRelayStatesToFirebase(relayStates);
  updateRelayOnCountsToFirebase(relayOnCounts);
  Serial.printf("1.5 Điều khiển máy tính bởi radar: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
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
      Serial.printf("1.6 Không đọc được trạng thái relay %d từ Firebase. Lý do: %s\n", i + 1, firebaseData.errorReason().c_str());
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
  Serial.printf("1.8 Boot count: %d\n", bootCount);
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

// 1.10 Hàm update trạng thái relay lên firebase
void updateRelayStatesToFirebase(bool status[]) {
  checkWiFiConnection();         // Kiểm tra kết nối Wi-Fi
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để cập nhật trạng thái
    String relaySPath = "/Computer/computer" + String(i + 1) + "/status";
    int relayState = status[i] ? 1 : 0;                           // Cập nhật trạng thái relay (1 = bật, 0 = tắt)
    if (Firebase.setInt(firebaseData, relaySPath, relayState)) {  // Gửi trạng thái lên Firebase cho relaySPath
      // Serial.printf("Relay %d status updated successfully at path: %s\n", i + 1, relaySPath.c_str());
    } else {
      Serial.printf("1.10 Failed to update Relay %d status at path: %s. Error: %s\n", i + 1, relaySPath.c_str(), firebaseData.errorReason().c_str());
    }
  }
  Serial.printf("1.10 updateRelayStatesToFirebase success");
}

// 1.11 Hàm update số lần bật tắt máy lên firebase
void updateRelayOnCountsToFirebase(int counts[]) {
  checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi
  String currentTime = getCurrentDate();
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để cập nhật số lần bật
    String relayCPath1 = "/ComputerOnCount/" + String(currentTime) + "/computer" + String(i + 1) + "/onCount";
    if (Firebase.setInt(firebaseData, relayCPath1, counts[i])) {  // Gửi số lần bật lên Firebase cho relayPath1
      // Serial.printf("1.11 Relay %d onCount updated successfully at path: %s\n", i + 1, relayCPath1.c_str());
    } else {
      Serial.printf("1.11 Failed to update Relay %d onCount at path: %s. Error: %s\n", i + 1, relayCPath1.c_str(), firebaseData.errorReason().c_str());
    }
  }
}

// 1.12 Hàm kiểm tra kết nối Wi-Fi và tự động kết nối lại nếu cần
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("1.12 Wi-Fi disconnected, attempting to reconnect...");
    WiFi.reconnect();  // Gọi lại Wi-Fi nếu bị mất kết nối
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    Serial.println("1.12 Wi-Fi reconnected! OK");
  }
}

// 1.13 Hàm điều khiển Relay
void TaskControlRelayMode(void* pvParameters) {
  for (;;) {
    // Kiểm tra kết nối Wi-Fi trước khi bắt đầu điều khiển relay
    checkWiFiConnection();  // Gọi hàm kiểm tra và kết nối lại Wi-Fi nếu cần
    if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {
      Serial.print("1.13 Free heap: ");
      Serial.println(esp_get_free_heap_size());
      Serial.print("1.13 relay core: ");
      Serial.println(xPortGetCoreID());

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
      Serial.println("1.13----------------TaskControlRelayMode: Accessing relay.");
      xSemaphoreGive(relayMutex);
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

// 1.14 Hàm upload nhiệt độ mỗi 30phuts
void TaskUploadTemperatureAfter30M(void* pvParameters) {
  for (;;) {
    checkWiFiConnection();  // Gọi hàm kiểm tra và kết nối lại Wi-Fi nếu cần
    if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {
      sendTemperatureAfter30Minutes();
      Serial.println("1.14----------------TaskUploadTemperatureAfter30M: Accessing relay.");
      xSemaphoreGive(relayMutex);
    }
    vTaskDelay(1800000 / portTICK_PERIOD_MS);
  }
}

void loop() {
}













// void resetRelayCountsDaily() {
//   // Lấy ngày hiện tại
//   String today = getCurrentDate();
//   if (today == "") {
//     return;  // Không thể lấy thời gian
//   }
//   // Mở Preferences
//   preferencesOnCount.begin("relay", false);
//   // Đọc ngày lưu trữ từ bộ nhớ
//   String savedDate = preferencesOnCount.getString("lastResetDate", "");
//   Serial.println("saveDate: ");
//   Serial.println(savedDate);
//   // So sánh ngày hiện tại và ngày lưu trữ
//   if (savedDate != today) {
//     // Ngày đã thay đổi, reset mảng
//     for (int i = 0; i < 4; i++) {
//       relayOnCounts[i] = 0;
//     }
//     Serial.println("Relay counts reset!");
//     // Lưu ngày hiện tại vào bộ nhớ
//     preferencesOnCount.putString("lastResetDate", today);
//   } else {
//     Serial.println("Relay counts are up-to-date. No reset needed.");
//   }
//   // Đóng Preferences
//   preferencesOnCount.end();
// }

// void loadRelayCounts() {
//   // Khôi phục mảng từ Preferences khi khởi động
//   preferencesOnCount.begin("relay", false);
//   if (preferencesOnCount.isKey("relayCounts")) {
//     preferencesOnCount.getBytes("relayCounts", relayOnCounts, sizeof(relayOnCounts));
//   }
//   preferencesOnCount.end();
// }


// #include <Arduino.h>
// #include <freertos/task.h>
// #include <freertos/FreeRTOS.h>
// #include "esp_task_wdt.h"
// #include <freertos/semphr.h>

// #include <Arduino_FreeRTOS.h>
// #include <semphr.h>

// Semaphore để đồng bộ hóa
// SemaphoreHandle_t sensorHLKSemaphore;
// SemaphoreHandle_t relaySemaphore;

// Hàm này đang không dùng
//1.1  Hàm set trạng thái auto cho việc bật máy tính
// void setAuto(bool isAuto) {
//     Serial.println("Setting Auto...");
//     if (Firebase.setBool(firebaseData, F("/isAuto/status"), isAuto)) {
//         Serial.println("Set Auto: ok");
//     } else {
//         Serial.printf("Set Auto failed: %s\n", firebaseData.errorReason().c_str());
//     }
// }

//1.2 Hàm get trạng thái auto trên firebase
// bool getAuto() {
//   if (Firebase.getBool(firebaseData, "/isAuto/status")) {
//     return firebaseData.boolData();
//   } else {
//     Serial.printf("Failed to get auto mode: %s\n", firebaseData.errorReason().c_str());
//     return false;
//   }
// }

// 1.2 v2 Hàm get trạng thái auto trên firebase
// bool getAuto() {
//   // Kiểm tra xem kết nối wifi có ổn định không trước khi thực hiện truy vấn
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi not connected. Please check connection.");
//     return false;
//   }
//   // Kiểm tra trạng thái từ Firebase
//   if (Firebase.getBool(firebaseData, "/isAuto/status")) {
//     if (firebaseData.boolData()) {
//       Serial.println("Auto mode is enabled: 1.");
//       return true;
//     } else {
//       Serial.println("Auto mode is disabled: 0.");
//       return false;
//     }
//   } else {
//     // In lỗi chi tiết nếu không thể lấy dữ liệu từ Firebase
//     Serial.printf("Failed to get auto mode: %s\n", firebaseData.errorReason().c_str());
//     // Thử lại nếu có lỗi kết nối Firebase
//     Serial.println("Retrying to fetch data getAuto function...");
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     return getAuto();  // Thử lại việc truy vấn
//   }
// }

//1.3 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
// void controlComputersForRadar() {
//   static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người (ticks)
//   TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (ticks)

//   int radarState = digitalRead(OUT_PIN);  // Đọc trạng thái radar

//   if (radarState == HIGH) {                               // Nếu có người
//     if (isAutoMode) {                                     // Chỉ xử lý nếu đang ở chế độ auto
//       if (manualToAutoSwitch) {                           // Chuyển từ chế độ manual sang auto
//         if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {  // Khóa mutex
//           // Kiểm tra xem tất cả các relay có đang tắt không
//           bool allRelaysOff = true;
//           for (int i = 0; i < 4; i++) {
//             if (relayStates[i]) {
//               allRelaysOff = false;
//               break;
//             }
//           }

//           if (allRelaysOff) {
//             Serial.println("Hệ thống đang ở chế độ auto, bật lại hai máy tính ngẫu nhiên.");
//             OnRelay();  // Hàm bật ngẫu nhiên hai máy tính
//           } else {
//             Serial.println("Có máy tính đang bật, không cần thực hiện bật lại.");
//           }

//           manualToAutoSwitch = false;  // Cập nhật trạng thái chuyển đổi
//           xSemaphoreGive(relayMutex);  // Mở khóa mutex
//         }
//       }
//     }
//     lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
//   } else {                            // Không có người
//     if (isAutoMode) {                 // Chỉ xử lý nếu đang ở chế độ auto
//       // Kiểm tra nếu không có người trong khoảng thời gian timeout
//       if ((currentTime - lastDetectionTime) >= SHUTDOWN_TIMEOUT) {
//         Serial.println("Không phát hiện người trong thời gian dài, tắt tất cả máy tính.");
//         if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {  // Khóa mutex
//           // Tắt từng máy tính và relay
//           for (int i = 0; i < 4; i++) {
//             if (relayStates[i]) {  // Chỉ tắt nếu relay đang bật
//               shutdownServer(serverIPs[i], i);
//               vTaskDelay(1000 / portTICK_PERIOD_MS);  // Chờ để đảm bảo máy tính shutdown hoàn toàn
//               digitalWrite(relays[i], HIGH);          // Ngắt relay
//               relayStates[i] = false;                 // Cập nhật trạng thái relay
//             }
//           }
//           xSemaphoreGive(relayMutex);  // Mở khóa mutex
//         }
//       }
//     }
//   }
//   updateRelayStatesToFirebase(relayStates);
//   updateRelayOnCountsToFirebase(relayOnCounts);
//   Serial.printf("Điều khiển máy tính bởi radar: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
// }

//1.4 Hàm điều khiển relay theo lệnh từ Firebase ở chế độ Manual
// void controlComputersManually() {
//   checkWiFiConnection();                                                 // Kiểm tra kết nối Wi-Fi
//   for (int i = 0; i < 4; i++) {                                           // Duyệt qua các relay và điều khiển dựa trên lệnh từ Firebase
//     String relayPath = "/Computer/computer" + String(i + 1) + "/status";  // Đường dẫn tới trạng thái nút điều khiển relay trong Firebase
//     // Đọc trạng thái relay từ Firebase
//     if (Firebase.getInt(firebaseData, relayPath)) {
//       int relayState = firebaseData.intData();  // Lấy trạng thái từ Firebase (1 = bật, 0 = tắt)
//       // Serial.printf("Ngay đây nè----lỗi đó");
//       // continue;  // Bỏ qua relay này nếu đọc Firebase thất bại

//       // Nếu trạng thái từ Firebase là "BẬT" và relay hiện tại đang "TẮT"
//       if (relayState == 1 && !relayStates[i]) {
//         Serial.printf("Bật relay %d thủ công.\n", i + 1);
//         digitalWrite(relays[i], LOW);  // Bật relay (mức LOW để bật)
//         relayOnCounts[i]++;            // Tăng số lần bật relay
//         vTaskDelay(1000 / portTICK_PERIOD_MS);
//         digitalWrite(relays[i], HIGH);  // Ngắt relay sau khi bật
//         relayStates[i] = true;          // Cập nhật trạng thái relay
//       }
//       // Nếu trạng thái từ Firebase là "TẮT" và relay hiện tại đang "BẬT"
//       else if (relayState == 0 && relayStates[i]) {
//         Serial.printf("Tắt relay %d thủ công.\n", i + 1);
//         shutdownServer(serverIPs[i], i);        // Gửi lệnh shutdown tới máy tính
//         vTaskDelay(2000 / portTICK_PERIOD_MS);  // Chờ khoảng 2 giây để máy tính shutdown hoàn toàn
//         digitalWrite(relays[i], HIGH);          // Ngắt relay
//         relayStates[i] = false;                 // Cập nhật trạng thái relay
//       }
//     } else {
//       Serial.println("------------------------------------Lỗi ngay chỗ hàm controlComputersManually");  // In ra lỗi nếu có
//       // Serial.println(firebaseData.errorReason());  // In ra lỗi nếu có
//       checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi
//     }
//   }
//   Serial.printf("Điều khiển máy tính ở chế độ thường: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
// }

//1.11 Hàm điều khiển Relay
// void TaskControlRelayMode(void* pvParameters) {
//   for(;;) {
//     Serial.print("Free heap: ");
//     Serial.println(esp_get_free_heap_size());
//     Serial.print("relay core: ");
//     Serial.println(xPortGetCoreID());

//     // Kiểm tra xem có ở chế độ tự động hay không
//     if (getAuto()) {
//       Serial.println("Đang ở chế độ auto");

//       // Cập nhật trạng thái cho đúng về isAuto
//       if (!isAutoMode) {
//         isAutoMode = true;          // Chuyển sang chế độ auto
//         manualToAutoSwitch = true;  // Đánh dấu vừa chuyển từ manual sang auto
//       }
//       Serial.print("Điều khiển máy tính theo cảm biến radar ");

//       // Lấy mutex trước khi điều khiển relay
//       if (xSemaphoreTake(relayMutex1, portMAX_DELAY)) {  // Chờ vô hạn để có quyền truy cập vào relay
//         controlComputersForRadar();                      // Điều khiển relay theo radar
//         xSemaphoreGive(relayMutex1);                     // Giải phóng mutex sau khi hoàn thành
//       }
//     } else {
//       isAutoMode = false;
//       Serial.println("Đang ở chế độ thủ công");

//       // Lấy mutex trước khi điều khiển relay
//       if (xSemaphoreTake(relayMutex1, portMAX_DELAY)) {  // Chờ vô hạn để có quyền truy cập vào relay
//         controlComputersManually();                      // Điều khiển relay thủ công
//         xSemaphoreGive(relayMutex1);                     // Giải phóng mutex sau khi hoàn thành
//       }
//     }
//     vTaskDelay(3500 / portTICK_PERIOD_MS);  // Đợi 3.5 giây trước khi lặp lại
//   }
// }

//1.4 Hàm điều khiển relay theo lệnh từ Firebase ở chế độ Manual
// void controlComputersManually() {
//   if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {                          // Khóa mutex
//     for (int i = 0; i < 4; i++) {                                           // Duyệt qua các relay và điều khiển dựa trên lệnh từ Firebase
//       String relayPath = "/Computer/computer" + String(i + 1) + "/status";  // Đường dẫn tới trạng thái nút điều khiển relay trong Firebase
//       if (Firebase.getInt(firebaseData, relayPath)) {
//         int relayState = firebaseData.intData();  // Lấy trạng thái từ Firebase (1 = bật, 0 = tắt)
//         // Nếu trạng thái từ Firebase là "BẬT" và relay hiện tại đang "TẮT"
//         if (relayState == 1 && !relayStates[i]) {
//           Serial.printf("Bật relay %d thủ công.\n", i + 1);
//           digitalWrite(relays[i], LOW);  // Bật relay (mức LOW để bật)
//           relayOnCounts[i]++;            // Tăng số lần bật relay
//           vTaskDelay(1000 / portTICK_PERIOD_MS);
//           digitalWrite(relays[i], HIGH);  // Ngắt relay sau khi bật
//           relayStates[i] = true;          // Cập nhật trạng thái relay
//         }
//         // Nếu trạng thái từ Firebase là "TẮT" và relay hiện tại đang "BẬT"
//         else if (relayState == 0 && relayStates[i]) {
//           Serial.printf("Tắt relay %d thủ công.\n", i + 1);
//           shutdownServer(serverIPs[i], i);        // Gửi lệnh shutdown tới máy tính
//           vTaskDelay(2000 / portTICK_PERIOD_MS);  // Chờ khoảng 2 giây để máy tính shutdown hoàn toàn
//           digitalWrite(relays[i], HIGH);          // Ngắt relay
//           relayStates[i] = false;                 // Cập nhật trạng thái relay
//         }
//       } else {
//         Serial.println(firebaseData.errorReason());  // In ra lỗi nếu có
//       }
//     }
//     xSemaphoreGive(relayMutex);  // Mở khóa mutex
//   }
//   Serial.printf("Điều khiển máy tính ở chế độ thường: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
// }

//1.0 Hàm khởi tạo thời gian
// bool setupTime() {
//   // Thiết lập múi giờ Việt Nam (GMT+7)
//   configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
//   struct tm timeinfo;
//   int retryCount = 0;        // Bộ đếm số lần thử
//   const int maxRetries = 3;  // Giới hạn số lần thử
//   // Thử lấy thời gian từ NTP server
//   while (!getLocalTime(&timeinfo) && retryCount < maxRetries) {
//     Serial.println("Đang cố gắng lấy thời gian...");
//     retryCount++;
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//   }
//   // Kiểm tra nếu vượt quá số lần thử
//   if (retryCount >= maxRetries) {
//     Serial.println("Lỗi: Không thể lấy thời gian sau nhiều lần thử!");
//     return false;  // Trả về false nếu thất bại
//   }
//   // In thời gian hiện tại nếu thành công
//   Serial.print("Lấy thời gian thành công: ");
//   Serial.println(&timeinfo, "Current time: %Y-%m-%d_%H:%M:%S");
//   return true;  // Trả về true nếu thành công
// }

//1.5 Hàm bật ngẫu nhiên 2 máy tính
// void OnRelay() {
//   // Mở bộ nhớ Preferences để đọc lại giá trị trước đó khi relay tắt điện
//   preferencesBootCount.begin("relay-app", false);               //Tham số đầu là gọi relay-app trong bộ nhớ, tham số thứ 2 là mở chế độ read/write
//   int bootCount = preferencesBootCount.getInt("bootCount", 0);  // Đọc giá trị của biến "bootCount" từ Preferences, nếu không có sẽ mặc định là 0
//   Serial.printf("Boot count: %d\n", bootCount);
//   turnOnRelaysByCombination(bootCount % 6, 4);  // Giới hạn tới 6 cặp relay // Gọi hàm bật relay theo số lần khởi động hiện tại
//   bootCount++;                                  // Tăng số lần khởi động cho lần khởi động tiếp theo
//   preferencesBootCount.putInt("bootCount", bootCount);   // Lưu giá trị mới của "bootCount" vào Preferences
//   preferencesBootCount.end();                            // Đóng Preferences
// }

//1.6 Hàm sinh tất cả các tổ hợp của 2 phần tử trong n phần tử
// void generateCombinations(int n, int k, int combinations[][2], int& count) {
//   int indices[k];
//   for (int i = 0; i < k; i++) {
//     indices[i] = i;
//   }
//   while (true) {
//     // Lưu tổ hợp hiện tại
//     for (int i = 0; i < k; i++) {
//       combinations[count][i] = indices[i];
//     }
//     count++;
//     // Tìm tổ hợp kế tiếp
//     int i = k - 1;
//     while (i >= 0 && indices[i] == n - k + i) {
//       i--;
//     }
//     if (i < 0) break;
//     indices[i]++;
//     for (int j = i + 1; j < k; j++) {
//       indices[j] = indices[i] + j - i;
//     }
//   }
// }

//1.7 Hàm bật relay theo cặp sinh ra từ tổ hợp
// void turnOnRelaysByCombination(int bootCount, int n) {
//   int combinations[100][2];                         // Mảng chứa tổ hợp (giả sử có tối đa 100 tổ hợp)
//   int count = 0;                                    // Biến đếm số lượng tổ hợp
//   generateCombinations(n, 2, combinations, count);  // Sinh tổ hợp 2 phần tử từ n phần tử
//   if (bootCount < 0 || bootCount >= count) {        // Kiểm tra xem bootCount có hợp lệ không
//     Serial.println("Invalid boot count");
//     return;
//   }
//   int relay1 = combinations[bootCount][0];
//   int relay2 = combinations[bootCount][1];
//   // Bật máy tính thứ 1 (relay1)
//   if (!relayStates[relay1]) {
//     digitalWrite(relays[relay1], LOW);  // Bật relay (kích mức thấp)
//     relayOnCounts[relay1]++;            // Tăng số lần bật của relay1
//     Serial.printf("Turned on relay %d\n", relay1 + 1);
//     vTaskDelay(1000 / portTICK_PERIOD_MS);  // Mô phỏng nhấn nút nguồn trong 1 giây
//     digitalWrite(relays[relay1], HIGH);     // Ngắt relay sau khi bật
//     relayStates[relay1] = true;             // Cập nhật trạng thái relay1
//   }
//   // Bật máy tính thứ 2 (relay2)
//   if (!relayStates[relay2]) {
//     digitalWrite(relays[relay2], LOW);  // Bật relay (kích mức thấp)
//     relayOnCounts[relay2]++;            // Tăng số lần bật của relay2
//     Serial.printf("Turned on relay %d\n", relay2 + 1);
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     digitalWrite(relays[relay2], HIGH);  // Ngắt relay sau khi bật
//     relayStates[relay2] = true;          // Cập nhật trạng thái relay2
//   }
//   // Cập nhật trạng thái relay lên Firebase
//   updateRelayStatesToFirebase(relayStates);
//   updateRelayOnCountsToFirebase(relayOnCounts);
// }


//1.10 Hàm điều khiển máy tính ở các chế độ dùng semaphore
// void TaskControlRelayMode(void* pvParameters) {
//     while (true) {
//         // Yêu cầu quyền sử dụng tài nguyên thông qua semaphore
//         if (xSemaphoreTake(relaySemaphore, (TickType_t)10) == pdTRUE) {
//             // Kiểm tra xem có ở chế độ tự động hay không
//             Serial.print("Free heap: ");
//             Serial.println(esp_get_free_heap_size());

//             if (getAuto()) {
//                 if (!isAutoMode) {
//                     isAutoMode = true;  // Chuyển sang chế độ auto
//                     manualToAutoSwitch = true;  // Đánh dấu việc vừa chuyển từ manual sang auto
//                 }
//                 controlComputersForRadar(); // Điều khiển relay theo radar
//                 vTaskDelay(1000 / portTICK_PERIOD_MS); // Nhả CPU trong 1s
//             } else {
//                 isAutoMode = false;
//                 controlComputersManually(); // Điều khiển relay thủ công
//                 vTaskDelay(1000 / portTICK_PERIOD_MS); // Nhả CPU trong 1s
//             }
//             // Kiểm tra và cập nhật trạng thái relay
//             Serial.printf("Updating relay states: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
//             // Trả lại quyền truy cập semaphore để các task khác có thể sử dụng
//             xSemaphoreGive(relaySemaphore);
//         } else {
//             Serial.println("Failed to acquire relay semaphore.");// Nếu không lấy được semaphore, thông báo lỗi
//         }
//         vTaskDelay(2000 / portTICK_PERIOD_MS);  // Đợi 2 giây trước khi tiếp tục
//     }
// }

//1.3 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
//  chế độ auto thì xem có người hay không. nếu không có người 15p thì tắt máy, có người thì không tắt máy
// void controlComputersForRadar() {
//   static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người (ticks)
//   TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (ticks)

//   int radarState = digitalRead(OUT_PIN);  // Đọc trạng thái radar
// // TH1: nếu có người mà ở chế độ auto thì phải bật lại 2 máy tính ngẫu nhiên do khi không có người đã tắt, nhưng phải thêm logic kiểm tra xem có máy tính đang bật hay không nếu ko có máy tính nào đang bật thì mới bật còn nếu có máy thì ko làm gì
//   if (radarState == HIGH) {  // Nếu có người
//     //Kiểm tra xem có bật chế độ auto không
//     if (manualToAutoSwitch) {
//       Serial.println("Hệ thống đang ở chế độ auto, Bật lại máy tính");
//       OnRelay(); // Hàm bật ngẫu nhiên hai máy tính
//       manualToAutoSwitch = false; //cập nhật trạng thái bật thủ công
//     }
//     lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
//   } else {                            // Không có người
//     // Kiểm tra nếu không có người trong 15 phút
//     if ((currentTime - lastDetectionTime) >= SHUTDOWN_TIMEOUT) {
//       // Duyệt qua từng địa chỉ IP và tắt máy tính
//       for (int i = 0; i < sizeof(serverIPs) / sizeof(serverIPs[0]); i++) {
//         shutdownServer(serverIPs[i], i);
//         vTaskDelay(1000 / portTICK_PERIOD_MS);  // Chờ 10 giây để máy tính shutdown hoàn toàn
//         digitalWrite(relays[i], HIGH);          // Ngắt relay
//         relayStates[i] = false;                 // Cập nhật trạng thái relay
//       }
//     }
//   }
// }//1.3 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
//  chế độ auto thì xem có người hay không. nếu không có người 15p thì tắt máy, có người thì không tắt máy
// void controlComputersForRadar() {
//   static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người (ticks)
//   TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (ticks)

//   int radarState = digitalRead(OUT_PIN);  // Đọc trạng thái radar
// // TH1: nếu có người mà ở chế độ auto thì phải bật lại 2 máy tính ngẫu nhiên do khi không có người đã tắt, nhưng phải thêm logic kiểm tra xem có máy tính đang bật hay không nếu ko có máy tính nào đang bật thì mới bật còn nếu có máy thì ko làm gì
//   if (radarState == HIGH) {  // Nếu có người
//     //Kiểm tra xem có bật chế độ auto không
//     if (manualToAutoSwitch) {
//       Serial.println("Hệ thống đang ở chế độ auto, Bật lại máy tính");
//       OnRelay(); // Hàm bật ngẫu nhiên hai máy tính
//       manualToAutoSwitch = false; //cập nhật trạng thái bật thủ công
//     }
//     lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
//   } else {                            // Không có người
//     // Kiểm tra nếu không có người trong 15 phút
//     if ((currentTime - lastDetectionTime) >= SHUTDOWN_TIMEOUT) {
//       // Duyệt qua từng địa chỉ IP và tắt máy tính
//       for (int i = 0; i < sizeof(serverIPs) / sizeof(serverIPs[0]); i++) {
//         shutdownServer(serverIPs[i], i);
//         vTaskDelay(1000 / portTICK_PERIOD_MS);  // Chờ 10 giây để máy tính shutdown hoàn toàn
//         digitalWrite(relays[i], HIGH);          // Ngắt relay
//         relayStates[i] = false;                 // Cập nhật trạng thái relay
//       }
//     }
//   }
// }

//1.4 Hàm điều khiển relay ở chế độ thông thường kiểu mới nhưng quên bỏ delay làm cái relay bật xong k tắt
// void controlComputersManually() {
//   for (int i = 0; i < 4; i++) {  // Duyệt qua các relay
//     String relayPath = "/Computer/computer" + String(i + 1) + "/status";  // Đường dẫn Firebase
//     if (Firebase.getInt(firebaseData, relayPath)) {
//       int relayState = firebaseData.intData();  // Lấy trạng thái từ Firebase (1 = bật, 0 = tắt)

//       if (relayState == 1 && !relayStates[i]) {  // BẬT relay nếu đang TẮT
//         Serial.printf("Bật relay %d thủ công.\n", i + 1);
//         controlRelay(i, LOW, true);  // Gọi hàm điều khiển bật relay
//       } else if (relayState == 0 && relayStates[i]) {  // TẮT relay nếu đang BẬT
//         Serial.printf("Tắt relay %d thủ công.\n", i + 1);
//         shutdownServer(serverIPs[i], i);  // Gửi lệnh shutdown tới máy tính
//         controlRelay(i, HIGH, false);    // Gọi hàm điều khiển tắt relay
//       }
//     } else {
//       Serial.printf("Lỗi Firebase tại relay %d: %s\n", i + 1, firebaseData.errorReason().c_str());
//     }
//   }
// }

// // Hàm hỗ trợ để điều khiển relay
// void controlRelay(int index, int state, bool isOn) {
//   digitalWrite(relays[index], state);  // Điều khiển relay
//   relayStates[index] = isOn;          // Cập nhật trạng thái relay
//   if (isOn) relayOnCounts[index]++;   // Tăng số lần bật nếu bật relay
// }

// các hàm nằm trong hàm setup
// Khởi tạo semaphore cho RELAY
// relaySemaphore = xSemaphoreCreateBinary();
// if (relaySemaphore != NULL) {
//   xSemaphoreGive(relaySemaphore);  // Khởi tạo semaphore ở trạng thái sẵn sàng
// }



// Khởi tạo task này nằm trong hàm setup
//           tên hàm   tên tượng trưng  stack null mức độ ưu tiên  handle của task
// xTaskCreate(TaskUploadTemperature, "TaskUploadTemperature", 3772, NULL, 1, NULL); //3710

// Create tasks for dual-core processing
// xTaskCreatePinnedToCore(
//   TaskUploadHLK,        // Task function
//   "TaskUploadHLK",      // Name of task
//   10192,                // Stack size
//   NULL,                 // Task input parameter
//   1,                    // Priority
//   &firebaseTaskHandle,  // Task handle
//   0                     // Core 0
// );
// vTaskDelay(500 / portTICK_PERIOD_MS);

// xTaskCreatePinnedToCore(
//   TaskControlRelayMode,    // Task function
//   "TaskControlRelayMode",  // Name of task
//   10192,                   // Stack size
//   NULL,                    // Task input parameter
//   1,                       // Priority
//   &relayTaskHandle,        // Task handle
//   1                        // Core 1
// );
// vTaskDelay(500 / portTICK_PERIOD_MS);


  // Tạo các task
  // xTaskCreate(
  //   TaskControlRelayMode,    // Task function
  //   "TaskControlRelayMode",  // Name of task
  //   84192,                   // Stack size
  //   NULL,                    // Task input parameter
  //   2,                       // Priority
  //   &relayTaskHandle         // Task handle
  // );
  // xTaskCreate(
  //   TaskUploadHLK,       // Task function
  //   "TaskUploadHLK",     // Name of task
  //   54192,               // Stack size
  //   NULL,                // Task input parameter
  //   1,                   // Priority
  //   &firebaseTaskHandle  // Task handle
  // );


// xTaskCreate(TaskRadarData, "TaskRadarData", 8196, NULL, 0, NULL);
// xTaskCreate(TaskTemperatureData, "TaskTemperatureData", 8196, NULL, 0, NULL);
// xTaskCreate(TaskPZEMData, "TaskPZEMData", 8192, NULL, 0, NULL);

// xTaskCreate(TaskControlRelayMode, "TaskControlRelayMode", 10192, NULL, 1, NULL);  //6796
// xTaskCreate(TaskUploadHLK, "TaskUploadHLK", 44192, NULL, 0, NULL);                //3710 12748 //6192

// // Cấu hình thời gian với múi giờ Việt Nam (GMT+7: 25200 giây)
// configTime(25200, 0, "pool.ntp.org", "time.nist.gov");  // Thiết lập múi giờ (Ví dụ: GMT+7 cho Việt Nam là 25200 giây)
// struct tm timeinfo;
// int retryCount = 0;        // Bộ đếm số lần thử
// const int maxRetries = 3;  // Giới hạn số lần thử

// while (!getLocalTime(&timeinfo) && retryCount < maxRetries) {
//   Serial.println("Đang cố gắng lấy thời gian...");
//   retryCount++;
//   vTaskDelay(1000 / portTICK_PERIOD_MS);
// }

// if (retryCount >= maxRetries) {
//   Serial.println("Lỗi: Không thể lấy thời gian sau nhiều lần thử!");
//   return;
// }
// // In thời gian hiện tại nếu thành công
// Serial.print("Lấy thời gian thành công: ");
// Serial.println(&timeinfo, "Current time: %Y-%m-%d_%H:%M:%S");