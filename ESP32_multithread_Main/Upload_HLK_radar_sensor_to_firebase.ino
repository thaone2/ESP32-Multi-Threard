// 2.1 Task cập nhật dữ liệu các cảm biến
void TaskUploadAllSensors(void* pvParameters) {
  for (;;) {
    checkWiFiConnection();
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      Serial.print("2.1: sensor core: ");
      Serial.println(xPortGetCoreID());
      // handleRadarData();  // Gửi trạng thái radar
      // vTaskDelay(500 / portTICK_PERIOD_MS);
      sendEnergyDataOncePerDay();  // Gửi dữ liệu năng lượng một lần mỗi ngày
      vTaskDelay(500 / portTICK_PERIOD_MS);
      handleTemperatureData();  // Gửi trạng thái nhiệt độ
      vTaskDelay(500 / portTICK_PERIOD_MS);
      handlePZEMData();  // Gửi trạng thái pzem
      vTaskDelay(500 / portTICK_PERIOD_MS);
      Serial.println("2.1 ----> Task Sensor Update: Accessing shared upload sensor.");
      xSemaphoreGive(relayMutex);
    } else {
      Serial.println("2.1 Failed to acquire relayMutex for upload from Sensor update firebase. ");
      Serial.println("2.1 Restarting Firebase...");
    }
    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }
}




// 2.2 Hàm xử lý dữ liệu radar
// void handleRadarData() {
//   int radarStatus = digitalRead(OUT_PIN);
//   checkWiFiConnection();
//   if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
//     Serial.printf("2.2 Updated Radar state successfully: %d\n", radarStatus);
//   } else {
//     // Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
//     Serial.printf("2.2 Failed to update radar state: bug is function handleRadarData");
//   }
// }
void handleRadarData() {
  // Đọc trạng thái radar từ chân OUT_PIN
  int radarStatus = digitalRead(OUT_PIN);

  // Kiểm tra kết nối Wi-Fi
  checkWiFiConnection();

  // Gửi dữ liệu lên Firebase
  if (Database.set<int>(aClient, "/HLK_RADAR/status", radarStatus)) {
    Serial.printf("2.2 Updated Radar state successfully: %d\n", radarStatus);
  } else {
    // In lỗi nếu cập nhật thất bại
    Serial.printf("2.2 Failed to update radar state: %s\n", aClient.lastError().message().c_str());
  }
}

// 2.3v2 Hàm xử lý dữ liệu nhiệt độ và gửi một lần qua JSON
// void handleTemperatureData() {
//   sensors1.requestTemperatures();
//   sensors2.requestTemperatures();
//   sensors3.requestTemperatures();
//   sensors4.requestTemperatures();

//   float temps[4] = {
//     sensors1.getTempCByIndex(0),
//     sensors2.getTempCByIndex(0),
//     sensors3.getTempCByIndex(0),
//     sensors4.getTempCByIndex(0)
//   };

//   // Tạo đối tượng JSON
//   FirebaseJson tempJson;
//   for (int i = 0; i < 4; i++) {
//     if (!isnan(temps[i])) {
//       String tempPath = "/computer" + String(i + 1) + "/temperature";
//       tempJson.set(tempPath.c_str(), temps[i]);
//     }
//   }
//   // Gửi toàn bộ dữ liệu nhiệt độ qua Firebase
//   if (Firebase.set(firebaseData, "/Temperatures", tempJson)) {
//     Serial.println("2.3 All temperature data sent successfully.");
//   } else {
//     Serial.printf("2.3 Failed to send temperature data: %s\n", firebaseData.errorReason().c_str());
//   }
// }

void handleTemperatureData() {
  // Đọc dữ liệu từ các cảm biến nhiệt độ
  sensors1.requestTemperatures();
  sensors2.requestTemperatures();
  sensors3.requestTemperatures();
  sensors4.requestTemperatures();

  float temps[4] = {
    sensors1.getTempCByIndex(0),
    sensors2.getTempCByIndex(0),
    sensors3.getTempCByIndex(0),
    sensors4.getTempCByIndex(0)
  };

  // Gửi từng giá trị nhiệt độ lên Firebase riêng lẻ
  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {  // Kiểm tra giá trị hợp lệ
      // Làm tròn đến 2 chữ số thập phân bằng round
      float roundedTemp = round(temps[i] * 100) / 100.0;

      // Tạo đường dẫn cho từng máy tính
      String temperaturePath = "/Temperatures/computer" + String(i + 1) + "/temperature";

      // Gửi dữ liệu lên Firebase
      Serial.printf("Updating %s with value %.2f... ", temperaturePath.c_str(), roundedTemp);
      bool statusUpdate = Database.set<float>(aClient, temperaturePath, roundedTemp);
      if (statusUpdate) {
        Serial.println("Success");
      } else {
        // Xử lý lỗi khi không gửi được dữ liệu
        Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
      }
    } else {
      Serial.printf("Temperature sensor %d returned an invalid value\n", i + 1);
    }
  }
}







// 2.4v2 Hàm xử lý dữ liệu PZEM và gửi một lần qua JSON
// void handlePZEMData() {
//   float voltage = pzem.voltage();
//   float current = pzem.current();
//   float frequency = pzem.frequency();
//   float power = pzem.power();
//   float energy = pzem.energy();
//   // Tạo đối tượng JSON
//   FirebaseJson pzemJson;
//   // Thêm dữ liệu vào JSON
//   pzemJson.set("voltage", isnan(voltage) ? 0 : voltage);
//   pzemJson.set("current", isnan(current) ? 0 : current);
//   pzemJson.set("frequency", isnan(frequency) ? 0 : frequency);
//   pzemJson.set("power", isnan(power) ? 0 : power);
//   pzemJson.set("energy", isnan(energy) ? 0 : energy);

//   // Gửi toàn bộ dữ liệu PZEM qua Firebase
//   if (Firebase.set(firebaseData, "/PZEM_Voltage", pzemJson)) {
//     Serial.println("2.4 All PZEM data sent successfully.");
//   } else {
//     Serial.printf("2.4 Failed to send PZEM data: %s\n", firebaseData.errorReason().c_str());
//   }
//   // Tổng hợp log
//   Serial.printf("2.4 PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
//                 voltage, current, frequency, power, energy);
// }
void handlePZEMData() {
  // Lấy dữ liệu từ cảm biến PZEM
  float voltage = pzem.voltage();
  float current = pzem.current();
  float frequency = pzem.frequency();
  float power = pzem.power();
  float energy = pzem.energy();

  // Làm tròn giá trị đến 2 chữ số thập phân
  voltage = isnan(voltage) ? 0 : round(voltage * 100) / 100.0;
  current = isnan(current) ? 0 : round(current * 100) / 100.0;
  frequency = isnan(frequency) ? 0 : round(frequency * 100) / 100.0;
  power = isnan(power) ? 0 : round(power * 100) / 100.0;
  energy = isnan(energy) ? 0 : round(energy * 100) / 100.0;

  // Gửi từng giá trị riêng lẻ lên Firebase
  String basePath = "/PZEM_Voltage";

  struct {
    const char* key;
    float value;
  } data[] = {
    { "voltage", voltage },
    { "current", current },
    { "frequency", frequency },
    { "power", power },
    { "energy", energy }
  };

  for (int i = 0; i < 5; i++) {
    String path = basePath + "/" + data[i].key;
    Serial.printf("Updating %s with value %.2f... ", path.c_str(), data[i].value);
    bool statusUpdate = Database.set<float>(aClient, path, data[i].value);
    if (statusUpdate) {
      Serial.println("Success");
    } else {
      Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
    }
  }

  // Tổng hợp log
  Serial.printf("2.4 PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
                voltage, current, frequency, power, energy);
}







// 2.5 Hàm kiểm tra và gửi năng lượng một lần mỗi ngày
// void sendEnergyDataOncePerDay() {
//   // Lấy ngày hiện tại
//   String currentDate = getCurrentDate();
//   String statusPath = String("/Energy_usage_status/") + String(currentDate);

//   // Kiểm tra trạng thái gửi trên Firebase nếu đã gửi thì không gửi nữa
//   if (Firebase.getInt(firebaseData, statusPath)) {
//     if (firebaseData.intData() == 1) {
//       // Trạng thái đã được đặt là 1, không cần gửi lại
//       Serial.println("2.5 Today's energy data already sent. Skipping...");
//       return;
//     }
//   } else {
//     // Trạng thái chưa tồn tại, tiến hành gửi dữ liệu
//     Serial.println("2.5 Status not found. Sending energy data...");
//     //reset relaycount mỗi ngày ké luôn
//     for (int i = 0; i < 4; i++) {
//       relayOnCounts[i] = 0;
//     }
//     Serial.println("2.5 Relay counts reset!");

//     float totalEnergy = pzem.energy();
//     if (isnan(totalEnergy)) totalEnergy = 0;  // Đảm bảo không gửi giá trị NaN

//     String currentTimeStr = getCurrentTime();
//     String energyPath = String("/Energy_use/") + String(currentTimeStr) + "/totalEnergy";

//     if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
//       Serial.printf("2.5 Energy data sent: %.2fkWh\n", totalEnergy);

//       // Sau khi gửi thành công, cập nhật trạng thái
//       if (Firebase.setInt(firebaseData, statusPath, 1)) {
//         Serial.println("2.5 Marked today's energy data as sent.");
//       } else {
//         Serial.printf("2.5 Failed to mark today's energy status: %s\n", firebaseData.errorReason().c_str());
//       }
//     } else {
//       Serial.printf("2.5 Failed to send energy data: %s\n", firebaseData.errorReason().c_str());
//     }
//   }
// }
void sendEnergyDataOncePerDay() {
  // Lấy ngày hiện tại
  String currentDate = getCurrentDate();
  String statusPath = "/Energy_usage_status/" + currentDate;

  // Kiểm tra trạng thái gửi trên Firebase
  int status = Database.get<int>(aClient, statusPath);
  if (aClient.lastError().code() == 0) {
    if (status == 1) {
      // Trạng thái đã được đặt là 1, không cần gửi lại
      Serial.println("2.5 Today's energy data already sent. Skipping...");
      return;
    }
  }

  // Trạng thái chưa tồn tại hoặc không có, tiến hành gửi dữ liệu
  Serial.println("2.5 Status not found or not sent. Sending energy data...");

  // Reset relay count mỗi ngày
  for (int i = 0; i < 4; i++) {
    relayOnCounts[i] = 0;
  }
  Serial.println("2.5 Relay counts reset!");

  // Lấy dữ liệu năng lượng từ PZEM
  float totalEnergy = pzem.energy();
  if (isnan(totalEnergy)) totalEnergy = 0;  // Đảm bảo không gửi giá trị NaN

  // Đường dẫn Firebase cho dữ liệu năng lượng
  String currentTimeStr = getCurrentTime();
  String energyPath = "/Energy_use/" + currentTimeStr + "/totalEnergy";

  // Gửi dữ liệu năng lượng
  if (Database.set<float>(aClient, energyPath, totalEnergy)) {
    Serial.printf("2.5 Energy data sent: %.2fkWh\n", totalEnergy);

    // Cập nhật trạng thái đã gửi
    if (Database.set<int>(aClient, statusPath, 1)) {
      Serial.println("2.5 Marked today's energy data as sent.");
    } else {
      // Lỗi khi cập nhật trạng thái
      Serial.printf("2.5 Failed to mark today's energy status: %s\n", aClient.lastError().message().c_str());
    }
  } else {
    // Lỗi khi gửi dữ liệu năng lượng
    Serial.printf("2.5 Failed to send energy data: %s\n", aClient.lastError().message().c_str());
  }
}

//2.6v2  Hàm gửi dữ liệu nhiệt độ mỗi 30 phút và gửi một lần qua JSON
// void sendTemperatureAfter30Minutes() {
//   // Lấy ngày giờ hiện tại
//   String currentTimeStr = getCurrentTime();
//   sensors1.requestTemperatures();
//   sensors2.requestTemperatures();
//   sensors3.requestTemperatures();
//   sensors4.requestTemperatures();

//   float temps[4] = {
//     sensors1.getTempCByIndex(0),
//     sensors2.getTempCByIndex(0),
//     sensors3.getTempCByIndex(0),
//     sensors4.getTempCByIndex(0)
//   };

//   // Tạo đối tượng JSON
//   FirebaseJson tempJson;

//   for (int i = 0; i < 4; i++) {
//     if (!isnan(temps[i])) {
//       String tempPath = String(currentTimeStr) + "/computer" + String(i + 1) + "/temperature";
//       tempJson.set(tempPath.c_str(), temps[i]);
//     }
//   }

//   // Gửi toàn bộ dữ liệu nhiệt độ qua Firebase
//   if (Firebase.updateNode(firebaseData, "/Temperatures_30", tempJson)) {
//     Serial.println("2.6 All temperature data sent successfully.");
//   } else {
//     Serial.printf("2.6 Failed to send temperature data: %s\n", firebaseData.errorReason().c_str());
//   }
// }

void sendTemperatureAfter30Minutes() {
  // Lấy ngày giờ hiện tại
  String currentTimeStr = getCurrentTime();

  // Yêu cầu dữ liệu từ các cảm biến
  sensors1.requestTemperatures();
  sensors2.requestTemperatures();
  sensors3.requestTemperatures();
  sensors4.requestTemperatures();

  // Lưu dữ liệu nhiệt độ vào mảng
  float temps[4] = {
    sensors1.getTempCByIndex(0),
    sensors2.getTempCByIndex(0),
    sensors3.getTempCByIndex(0),
    sensors4.getTempCByIndex(0)
  };

  // Làm tròn nhiệt độ và gửi từng giá trị lên Firebase
  String basePath = "/Temperatures_30/" + currentTimeStr;

  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {  // Kiểm tra giá trị hợp lệ
      // Làm tròn nhiệt độ đến 2 chữ số thập phân
      float roundedTemp = round(temps[i] * 100) / 100.0;

      // Đường dẫn cho từng máy tính
      String path = basePath + "/computer" + String(i + 1) + "/temperature";

      // Gửi giá trị lên Firebase
      Serial.printf("Updating %s with value %.2f... ", path.c_str(), roundedTemp);
      bool statusUpdate = Database.set<float>(aClient, path, roundedTemp);
      if (statusUpdate) {
        Serial.println("Success");
      } else {
        Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
      }
    } else {
      Serial.printf("Temperature for computer%d is invalid (NaN).\n", i + 1);
    }
  }
}
