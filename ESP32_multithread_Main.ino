#include <WiFi.h>
#include <OneWire.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <PZEM004Tv30.h>
#include <FirebaseESP32.h>
#include <HardwareSerial.h>
#include <DallasTemperature.h>

// #include <time.h>
// #include <Arduino.h>
// #include <freertos/task.h>
// #include <freertos/FreeRTOS.h>
// #include "esp_task_wdt.h"
// #include <freertos/semphr.h>

// #include <Arduino_FreeRTOS.h>
// #include <semphr.h>

// Thông tin Firebase
#define FIREBASE_HOST "https://todolistapp-408f2-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_AUTH "iej75ZGaEnpVDwfR67qv82qSa7GNq60DJf9AO5ig"

// Semaphore để đồng bộ hóa
// SemaphoreHandle_t sensorHLKSemaphore;
// SemaphoreHandle_t relaySemaphore;

#define WIFI_SSID "Camera1"
#define WIFI_PASSWORD "22052020"

#define SHUTDOWN_TIMEOUT (900000 / portTICK_PERIOD_MS)  // 15 phút (đơn vị ticks)
const char* serverIPs[] = {
  "192.168.1.19",
  "192.168.1.78",
  "192.168.142.118",
  "192.168.1.104"
};
const int serverPort = 8080;

// cảm biến HLK-LD2410B-P
#define OUT_PIN 15  //HLK-LD2410B-P
HardwareSerial mySerial(1);

// Cảm biến DS18B20
#define OUT_TEM_1 12  // Pin for the DS18B20 c1 la 13
#define OUT_TEM_2 12  // Pin for the DS18B20 c2 la 12
#define OUT_TEM_3 12  // Pin for the DS18B20 c3 la 14
#define OUT_TEM_4 12  // Pin for the DS18B20 c4 la 27
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

Preferences preferences;

// Relay
#define RELAY_1_PIN 5   // GPIO18 cho relay 1 (máy tính 1)
#define RELAY_2_PIN 18  // GPIO19 cho relay 2 (máy tính 2)
#define RELAY_3_PIN 19  // GPIO21 cho relay 3 (máy tính 3)
#define RELAY_4_PIN 21  // GPIO22 cho relay 4 (máy tính 4)
bool relayStates[4] = { false, false, false, false };
bool isAutoMode = false;
bool manualToAutoSwitch = false;
int relayOnCounts[4] = { 0, 0, 0, 0 };  // Số lần bật của từng relay
int relays[] = { RELAY_1_PIN, RELAY_2_PIN, RELAY_3_PIN, RELAY_4_PIN };


void setup() {
  // Khởi tạo semaphore cho RELAY
  // relaySemaphore = xSemaphoreCreateBinary();
  // if (relaySemaphore != NULL) {
  //   xSemaphoreGive(relaySemaphore);  // Khởi tạo semaphore ở trạng thái sẵn sàng
  // }

  Serial.begin(115200);
  pzemSerial.begin(9600, SERIAL_8N1, RXD2, TXD2);  //Pzem
  mySerial.begin(256000, SERIAL_8N1);              //HLK
  String currentTime = getCurrentTime();
  pinMode(OUT_PIN, INPUT);
  pinMode(RELAY_1_PIN, OUTPUT);  // Cấu hình relay là OUTPUT
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
  //Wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print("...");
  }
  Serial.println("\nConnected to WiFi");

  // firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Cấu hình thời gian với múi giờ Việt Nam (GMT+7: 25200 giây)
  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");  // Thiết lập múi giờ (Ví dụ: GMT+7 cho Việt Nam là 25200 giây)
  struct tm timeinfo;
  int retryCount = 0;        // Bộ đếm số lần thử
  const int maxRetries = 3;  // Giới hạn số lần thử

  while (!getLocalTime(&timeinfo) && retryCount < maxRetries) {
    Serial.println("Đang cố gắng lấy thời gian...");
    retryCount++;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  if (retryCount >= maxRetries) {
    Serial.println("Lỗi: Không thể lấy thời gian sau nhiều lần thử!");
    return;
  }
  // In thời gian hiện tại nếu thành công
  Serial.print("Lấy thời gian thành công: ");
  Serial.println(&timeinfo, "Current time: %Y-%m-%d_%H:%M:%S");

  // Khởi tạo task
  //           tên hàm   tên tượng trưng  stack null mức độ ưu tiên  handle của task
  // xTaskCreate(TaskUploadTemperature, "TaskUploadTemperature", 3772, NULL, 1, NULL); //3710

  xTaskCreate(TaskControlRelayMode, "TaskControlRelayMode", 10192, NULL, 1, NULL);  //6796
  xTaskCreate(TaskUploadHLK, "TaskUploadHLK", 44192, NULL, 0, NULL);                //3710 12748 //6192

  // xTaskCreate(TaskRadarData, "TaskRadarData", 8196, NULL, 0, NULL);
  // xTaskCreate(TaskTemperatureData, "TaskTemperatureData", 8196, NULL, 0, NULL);
  // xTaskCreate(TaskPZEMData, "TaskPZEMData", 8192, NULL, 0, NULL);
}

//1.1 Hàm lấy thời gian
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Lỗi không lấy được thời gian";
  }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H:%M:%SS", &timeinfo);  // Định dạng thời gian
  return String(timeString);
}

// Hàm này đang không dùng
//1.2  Hàm set trạng thái auto cho việc bật máy tính
// void setAuto(bool isAuto) {
//     Serial.println("Setting Auto...");
//     if (Firebase.setBool(firebaseData, F("/isAuto/status"), isAuto)) {
//         Serial.println("Set Auto: ok");
//     } else {
//         Serial.printf("Set Auto failed: %s\n", firebaseData.errorReason().c_str());
//     }
// }

//1.3 Hàm get trạng thái auto trên firebase
bool getAuto() {
  if (Firebase.getBool(firebaseData, "/isAuto/status")) {
    return firebaseData.boolData();
  } else {
    Serial.printf("Failed to get auto mode: %s\n", firebaseData.errorReason().c_str());
    return false;
  }
}

//1.4 Hàm kiểm tra cảm biến radar và điều khiển relay ở chế độ Auto sử dụng tick cho rtos
void controlComputersForRadar() {
  static TickType_t lastDetectionTime = 0;       // Thời gian cuối cùng phát hiện người (ticks)
  TickType_t currentTime = xTaskGetTickCount();  // Thời gian hiện tại (ticks)

  int radarState = digitalRead(OUT_PIN);  // Đọc trạng thái radar
  if (radarState == HIGH) {               // Nếu có người
    if (manualToAutoSwitch) {
      Serial.println("Đã phát hiện sự hiện diện. Bật ngẫu nhiên 2 máy tính");
      OnRelay();
      manualToAutoSwitch = false;
    }
    lastDetectionTime = currentTime;  // Cập nhật thời gian phát hiện người
  } else {                            // Không có người
    // Kiểm tra nếu không có người trong 15 phút
    if ((currentTime - lastDetectionTime) >= SHUTDOWN_TIMEOUT) {
      // Duyệt qua từng địa chỉ IP và tắt máy tính
      for (int i = 0; i < sizeof(serverIPs) / sizeof(serverIPs[0]); i++) {
        shutdownServer(serverIPs[i], i);
        vTaskDelay(10000 / portTICK_PERIOD_MS);  // Chờ 10 giây để máy tính shutdown hoàn toàn
        digitalWrite(relays[i], HIGH);           // Ngắt relay
        relayStates[i] = false;                  // Cập nhật trạng thái relay
      }
    }
  }
}

//1.5 Hàm điều khiển relay theo lệnh từ Firebase ở chế độ Manual
void controlComputersManually() {
  for (int i = 0; i < 4; i++) {                                           // Duyệt qua các relay và điều khiển dựa trên lệnh từ Firebase
    String relayPath = "/Computer/computer" + String(i + 1) + "/status";  // Đường dẫn tới trạng thái nút điều khiển relay trong Firebase
    if (Firebase.getInt(firebaseData, relayPath)) {
      int relayState = firebaseData.intData();  // Lấy trạng thái từ Firebase (1 = bật, 0 = tắt)
      // Nếu trạng thái từ Firebase là "BẬT" và relay hiện tại đang "TẮT"
      if (relayState == 1 && !relayStates[i]) {
        Serial.printf("Bật relay %d thủ công.\n", i + 1);
        digitalWrite(relays[i], LOW);  // Bật relay (mức LOW để bật)
        relayOnCounts[i]++;            // Tăng số lần bật relay
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        digitalWrite(relays[i], HIGH);  // Ngắt relay sau khi bật
        relayStates[i] = true;          // Cập nhật trạng thái relay
      }
      // Nếu trạng thái từ Firebase là "TẮT" và relay hiện tại đang "BẬT"
      else if (relayState == 0 && relayStates[i]) {
        Serial.printf("Tắt relay %d thủ công.\n", i + 1);
        shutdownServer(serverIPs[i], i);        // Gửi lệnh shutdown tới máy tính
        vTaskDelay(2000 / portTICK_PERIOD_MS);  // Chờ khoảng 2 giây để máy tính shutdown hoàn toàn
        digitalWrite(relays[i], HIGH);          // Ngắt relay
        relayStates[i] = false;                 // Cập nhật trạng thái relay
      }
    } else {
      Serial.println(firebaseData.errorReason());  // In ra lỗi nếu có
    }
  }
}

//1.6 Hàm bật ngẫu nhiên 2 máy tính
void OnRelay() {
  // Mở bộ nhớ Preferences để đọc lại giá trị trước đó khi relay tắt điện
  preferences.begin("relay-app", false);               //Tham số đầu là gọi relay-app trong bộ nhớ, tham số thứ 2 là mở chế độ read/write
  int bootCount = preferences.getInt("bootCount", 0);  // Đọc giá trị của biến "bootCount" từ Preferences, nếu không có sẽ mặc định là 0
  Serial.printf("Boot count: %d\n", bootCount);
  turnOnRelaysByCombination(bootCount % 6, 4);  // Giới hạn tới 6 cặp relay // Gọi hàm bật relay theo số lần khởi động hiện tại
  bootCount++;                                  // Tăng số lần khởi động cho lần khởi động tiếp theo
  preferences.putInt("bootCount", bootCount);   // Lưu giá trị mới của "bootCount" vào Preferences
  preferences.end();                            // Đóng Preferences
}
//1.7 Hàm sinh tất cả các tổ hợp của 2 phần tử trong n phần tử
void generateCombinations(int n, int k, int combinations[][2], int& count) {
  int indices[k];
  for (int i = 0; i < k; i++) {
    indices[i] = i;
  }
  while (true) {
    // Lưu tổ hợp hiện tại
    for (int i = 0; i < k; i++) {
      combinations[count][i] = indices[i];
    }
    count++;
    // Tìm tổ hợp kế tiếp
    int i = k - 1;
    while (i >= 0 && indices[i] == n - k + i) {
      i--;
    }
    if (i < 0) break;
    indices[i]++;
    for (int j = i + 1; j < k; j++) {
      indices[j] = indices[i] + j - i;
    }
  }
}

//1.8 Hàm bật relay theo cặp sinh ra từ tổ hợp
void turnOnRelaysByCombination(int bootCount, int n) {
  int combinations[100][2];                         // Mảng chứa tổ hợp (giả sử có tối đa 100 tổ hợp)
  int count = 0;                                    // Biến đếm số lượng tổ hợp
  generateCombinations(n, 2, combinations, count);  // Sinh tổ hợp 2 phần tử từ n phần tử
  if (bootCount < 0 || bootCount >= count) {        // Kiểm tra xem bootCount có hợp lệ không
    Serial.println("Invalid boot count");
    return;
  }
  int relay1 = combinations[bootCount][0];
  int relay2 = combinations[bootCount][1];
  // Bật máy tính thứ 1 (relay1)
  if (!relayStates[relay1]) {
    digitalWrite(relays[relay1], LOW);  // Bật relay (kích mức thấp)
    relayOnCounts[relay1]++;            // Tăng số lần bật của relay1
    Serial.printf("Turned on relay %d\n", relay1 + 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Mô phỏng nhấn nút nguồn trong 1 giây
    digitalWrite(relays[relay1], HIGH);     // Ngắt relay sau khi bật
    relayStates[relay1] = true;             // Cập nhật trạng thái relay1
  }
  // Bật máy tính thứ 2 (relay2)
  if (!relayStates[relay2]) {
    digitalWrite(relays[relay2], LOW);  // Bật relay (kích mức thấp)
    relayOnCounts[relay2]++;            // Tăng số lần bật của relay2
    Serial.printf("Turned on relay %d\n", relay2 + 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    digitalWrite(relays[relay2], HIGH);  // Ngắt relay sau khi bật
    relayStates[relay2] = true;          // Cập nhật trạng thái relay2
  }
  // Cập nhật trạng thái relay lên Firebase
  updateRelayStatesToFirebase(relayStates);
  updateRelayOnCountsToFirebase(relayOnCounts);
}

//1.9 Hàm update trạng thái relay lên firebase
void updateRelayStatesToFirebase(bool status[]) {
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để cập nhật trạng thái
    String relaySPath = "/Computer/computer" + String(i + 1) + "/status";
    String relaySPath1 = "/Computerstatus/computer" + String(i + 1) + "/status";
    int relayState = status[i] ? 1 : 0;                           // Cập nhật trạng thái relay (1 = bật, 0 = tắt)
    if (Firebase.setInt(firebaseData, relaySPath, relayState)) {  // Gửi trạng thái lên Firebase cho relaySPath
                                                                  // Serial.printf("Relay %d status updated successfully at path: %s\n", i + 1, relaySPath.c_str());
    } else {
      Serial.printf("Failed to update Relay %d status at path: %s. Error: %s\n", i + 1, relaySPath.c_str(), firebaseData.errorReason().c_str());
    }
    if (Firebase.setInt(firebaseData, relaySPath1, relayState)) {  // Gửi trạng thái lên Firebase cho relaySPath1
                                                                   // Serial.printf("Relay %d status updated successfully at path: %s\n", i + 1, relaySPath1.c_str());
    } else {
      Serial.printf("Failed to update Relay %d status at path: %s. Error: %s\n", i + 1, relaySPath1.c_str(), firebaseData.errorReason().c_str());
    }
  }
}

//1.10 Hàm update số lần bật tắt máy lên firebase
void updateRelayOnCountsToFirebase(int counts[]) {
  for (int i = 0; i < 4; i++) {  // Duyệt qua tất cả các relay để cập nhật số lần bật
    String relayCPath1 = "/Computerstatus/computer" + String(i + 1) + "/onCount";
    if (Firebase.setInt(firebaseData, relayCPath1, counts[i])) {  // Gửi số lần bật lên Firebase cho relayPath1
                                                                  // Serial.printf("Relay %d onCount updated successfully at path: %s\n", i + 1, relayCPath1.c_str());
    } else {
      Serial.printf("Failed to update Relay %d onCount at path: %s. Error: %s\n", i + 1, relayCPath1.c_str(), firebaseData.errorReason().c_str());
    }
  }
}

//1.11 Hàm điều khiển máy tính ở các chế độ
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

void TaskControlRelayMode(void* pvParameters) {
  while (true) {
    Serial.print("Free heap: ");
    Serial.println(esp_get_free_heap_size());
    // Kiểm tra xem có ở chế độ tự động hay không
    if (getAuto()) {
      if (!isAutoMode) {
        isAutoMode = true;          // Chuyển sang chế độ auto
        manualToAutoSwitch = true;  // Đánh dấu vừa chuyển từ manual sang auto
      }
      controlComputersForRadar();  // Điều khiển relay theo radar
    } else {
      isAutoMode = false;
      controlComputersManually();  // Điều khiển relay thủ công
    }
    // Kiểm tra và cập nhật trạng thái relay
    Serial.printf("Updating relay states: %d %d %d %d\n", relayStates[0], relayStates[1], relayStates[2], relayStates[3]);
    // Nhả CPU trong 3 giây trước khi tiếp tục
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}



void loop() {
}
