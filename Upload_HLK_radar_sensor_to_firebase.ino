// 2.1 Task cập nhật dữ liệu các cảm biến
void TaskUploadAllSensors(void* pvParameters) {
  for (;;) {
    checkWiFiConnection();
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
    // if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {
    // if (xSemaphoreTake(relayMutex, 1000 / portTICK_PERIOD_MS)) {
      Serial.print("2.1: sensor core: ");
      Serial.println(xPortGetCoreID());
      sendEnergyDataOncePerDay(); // Gửi dữ liệu năng lượng một lần mỗi ngày
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      handleRadarData(); // Gửi trạng thái radar
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      handleTemperatureData(); // Gửi trạng thái nhiệt độ
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      handlePZEMData(); // Gửi trạng thái pzem
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      Serial.println("2.1 ----> Task Sensor Update: Accessing shared upload sensor.");
      xSemaphoreGive(relayMutex);
    } else {
      Serial.println("2.1 Failed to acquire relayMutex for upload from Sensor update firebase. ");
      Serial.println("2.1 Restarting Firebase...");
      Firebase.begin(&config, &auth);  // Tái khởi tạo Firebase
      Firebase.reconnectWiFi(true);    // Tự động kết nối lại Wi-Fi
    }
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

// 2.2 Hàm xử lý dữ liệu radar
void handleRadarData() {
  int radarStatus = digitalRead(OUT_PIN);
  checkWiFiConnection();
  if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
    Serial.printf("2.2 Updated Radar state successfully: %d\n", radarStatus);
  } else {
    // Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
    Serial.printf("2.2 Failed to update radar state: bug is function handleRadarData");
  }
}
// 2.3v2 Hàm xử lý dữ liệu nhiệt độ và gửi một lần qua JSON
void handleTemperatureData() {
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

  // Tạo đối tượng JSON
  FirebaseJson tempJson;
  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      String tempPath = "computer" + String(i + 1) + "/temperature";
      tempJson.set(tempPath.c_str(), temps[i]);
    }
  }
  // Gửi toàn bộ dữ liệu nhiệt độ qua Firebase
  if (Firebase.set(firebaseData, "/Temperatures", tempJson)) {
    Serial.println("2.3 All temperature data sent successfully.");
  } else {
    Serial.printf("2.3 Failed to send temperature data: %s\n", firebaseData.errorReason().c_str());
  }
}

// 2.4v2 Hàm xử lý dữ liệu PZEM và gửi một lần qua JSON
void handlePZEMData() {
  float voltage = pzem.voltage();
  float current = pzem.current();
  float frequency = pzem.frequency();
  float power = pzem.power();
  float energy = pzem.energy();
  // Tạo đối tượng JSON
  FirebaseJson pzemJson;
  // Thêm dữ liệu vào JSON
  pzemJson.set("voltage", isnan(voltage) ? 0 : voltage);
  pzemJson.set("current", isnan(current) ? 0 : current);
  pzemJson.set("frequency", isnan(frequency) ? 0 : frequency);
  pzemJson.set("power", isnan(power) ? 0 : power);
  pzemJson.set("energy", isnan(energy) ? 0 : energy);

  // Gửi toàn bộ dữ liệu PZEM qua Firebase
  if (Firebase.set(firebaseData, "/PZEM_Voltage", pzemJson)) {
    Serial.println("2.4 All PZEM data sent successfully.");
  } else {
    Serial.printf("2.4 Failed to send PZEM data: %s\n", firebaseData.errorReason().c_str());
  }
  // Tổng hợp log
  Serial.printf("2.4 PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
                voltage, current, frequency, power, energy);
}
// 2.5 Hàm kiểm tra và gửi năng lượng một lần mỗi ngày
void sendEnergyDataOncePerDay() {
  // Lấy ngày hiện tại
  String currentDate = getCurrentDate();
  String statusPath = String("/Energy_usage_status/") + String(currentDate);

  // Kiểm tra trạng thái gửi trên Firebase nếu đã gửi thì không gửi nữa
  if (Firebase.getInt(firebaseData, statusPath)) {
    if (firebaseData.intData() == 1) {
      // Trạng thái đã được đặt là 1, không cần gửi lại
      Serial.println("2.5 Today's energy data already sent. Skipping...");
      return;
    }
  } else {
    // Trạng thái chưa tồn tại, tiến hành gửi dữ liệu
    Serial.println("2.5 Status not found. Sending energy data...");
    //reset relaycount mỗi ngày ké luôn
    for (int i = 0; i < 4; i++) {
      relayOnCounts[i] = 0;
    }
    Serial.println("2.5 Relay counts reset!");

    float totalEnergy = pzem.energy();
    if (isnan(totalEnergy)) totalEnergy = 0;  // Đảm bảo không gửi giá trị NaN

    String currentTimeStr = getCurrentTime();
    String energyPath = String("/Energy_use/") + String(currentTimeStr) + "/totalEnergy";

    if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
      Serial.printf("2.5 Energy data sent: %.2fkWh\n", totalEnergy);

      // Sau khi gửi thành công, cập nhật trạng thái
      if (Firebase.setInt(firebaseData, statusPath, 1)) {
        Serial.println("2.5 Marked today's energy data as sent.");
      } else {
        Serial.printf("2.5 Failed to mark today's energy status: %s\n", firebaseData.errorReason().c_str());
      }
    } else {
      Serial.printf("2.5 Failed to send energy data: %s\n", firebaseData.errorReason().c_str());
    }
  }
}
//2.6v2  Hàm gửi dữ liệu nhiệt độ mỗi 30 phút và gửi một lần qua JSON
void sendTemperatureAfter30Minutes() {
  // Lấy ngày giờ hiện tại
  String currentTimeStr = getCurrentTime();
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

  // Tạo đối tượng JSON
  FirebaseJson tempJson;

  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      String tempPath = String(currentTimeStr) + "/computer" + String(i + 1) + "/temperature";
      tempJson.set(tempPath.c_str(), temps[i]);
    }
  }

  // Gửi toàn bộ dữ liệu nhiệt độ qua Firebase
  if (Firebase.updateNode(firebaseData, "/Temperatures_30", tempJson)) {
    Serial.println("2.6 All temperature data sent successfully.");
  } else {
    Serial.printf("2.6 Failed to send temperature data: %s\n", firebaseData.errorReason().c_str());
  }
}





// 2.3 Hàm xử lý dữ liệu nhiệt độ
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
//   for (int i = 0; i < 4; i++) {
//     if (!isnan(temps[i])) {
//       String tempPath = "/Temperatures/computer" + String(i + 1) + "/temperature";
//       if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
//         Serial.printf("2.3 Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
//       } else {
//         // Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
//         Serial.printf("2.3 Failed to send temperature of computer: function handleTemperatureData");
//       }
//     }
//   }
// }

// 2.4 Hàm xử lý dữ liệu PZEM
// void handlePZEMData() {
//   // Đọc dữ liệu từ PZEM
//   float voltage = pzem.voltage();
//   float current = pzem.current();
//   float frequency = pzem.frequency();
//   float power = pzem.power();
//   float energy = pzem.energy();

//   // Gửi dữ liệu lên Firebase
//   if (!isnan(voltage)) {
//     if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage)) {
//       Serial.printf("2.4 Voltage sent: %.2fV\n", voltage);
//     } else {
//       Serial.printf("2.4 Failed to send voltage: %.2fV, Error: %s\n", voltage, firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.println("2.4 Voltage data invalid (NaN).");
//   }

//   if (!isnan(current)) {
//     if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current)) {
//       Serial.printf("2.4 Current sent: %.2fA\n", current);
//     } else {
//       Serial.printf("2.4 Failed to send current: %.2fA, Error: %s\n", current, firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.println("2.4 Current data invalid (NaN).");
//   }

//   if (!isnan(frequency)) {
//     if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency)) {
//       Serial.printf("2.4 Frequency sent: %.2fHz\n", frequency);
//     } else {
//       Serial.printf("2.4 Failed to send frequency: %.2fHz, Error: %s\n", frequency, firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.println("2.4 Frequency data invalid (NaN).");
//   }

//   if (!isnan(power)) {
//     if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power)) {
//       Serial.printf("2.4 Power sent: %.2fW\n", power);
//     } else {
//       Serial.printf("2.4 Failed to send power: %.2fW, Error: %s\n", power, firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.println("2.4 Power data invalid (NaN).");
//   }

//   if (!isnan(energy)) {
//     if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/energy", energy)) {
//       Serial.printf("2.4 Energy sent: %.2fkWh\n", energy);
//     } else {
//       Serial.printf("2.4 Failed to send energy: %.2fkWh, Error: %s\n", energy, firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.println("2.4 Energy data invalid (NaN).");
//   }

//   // Tổng hợp log
//   Serial.printf("2.4 PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
//                 voltage, current, frequency, power, energy);
// }

// 2.6 Hàm gửi dữ liệu nhiệt độ mỗi 30p
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
//   for (int i = 0; i < 4; i++) {
//     if (!isnan(temps[i])) {
//       String tempPath = "/Temperatures_30/" + String(currentTimeStr) + "/computer" + String(i + 1) + "/temperature";
//       if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
//         Serial.printf("2.6 Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
//       } else {
//         // Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
//         Serial.printf("2.6 Failed to send temperature of computer: function sendTemperatureAfter30Minutes");
//       }
//     }
//   }
// }





// struct tm timeinfo;
// if (!getLocalTime(&timeinfo)) {
//   Serial.println("Failed to get local time.");
// }

// char currentDate[11];
// snprintf(currentDate, sizeof(currentDate), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);

// struct tm timeinfo;
// if (!getLocalTime(&timeinfo)) {
//   Serial.println("Failed to get local time.");
// }

// char currentTimeStr[20];
// snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d_%02d:%02d:%02d",
//          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
//          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

// char currentTimeStr[20];

// snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d_%02d:%02d:%02d",
//          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
//          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);


// Hàm xử lý dữ liệu PZEM kiểu cũ, cũng ok nhưng ko có phát hiện lỗi
// void handlePZEMData() {
//   float voltage = pzem.voltage();
//   float current = pzem.current();
//   float frequency = pzem.frequency();
//   float power = pzem.power();
//   float energy = pzem.energy();

//   checkWiFiConnection();// Kiểm tra kết nối Wi-Fi

//   if (!isnan(voltage)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
//   if (!isnan(current)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
//   if (!isnan(frequency)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
//   if (!isnan(power)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);
//   if (!isnan(energy)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/energy", energy);

//   Serial.printf("PZEM Data sent: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n", voltage, current, frequency, power, energy);
// }

// Hàm kiểm tra và gửi năng lượng một lần mỗi ngày
// void sendEnergyDataOncePerDay() {
//   struct tm timeinfo;
//   if (!getLocalTime(&timeinfo)) {
//     Serial.println("Failed to get local time.");
//     return;
//   }

//   // Lấy ngày hiện tại
//   char currentDate[11];
//   snprintf(currentDate, sizeof(currentDate), "%04d-%02d-%02d",
//            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
//   String statusPath = String("/Energy_use_status/") + String(currentDate);

//   // Kiểm tra trạng thái gửi trên Firebase
//   if (Firebase.getInt(firebaseData, statusPath)) {
//     if (firebaseData.intData() == 1) {
//       Serial.println("Today's energy data already sent. Skipping...");
//       return;
//     }
//   } else if (firebaseData.httpCode() != FIREBASE_ERROR_HTTP_CODE_NOT_FOUND) {
//     Serial.printf("Failed to check energy status: %s\n", firebaseData.errorReason().c_str());
//     return;
//   }

//   // Gửi dữ liệu nếu chưa gửi hôm nay
//   float totalEnergy = pzem.energy();
//   if (isnan(totalEnergy)) totalEnergy = 0;

//   char currentTimeStr[20];
//   snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d_%02d:%02d:%02d",
//            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
//            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

//   String energyPath = String("/Energy_use/") + String(currentTimeStr) + "/totalEnergy";

//   if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
//     Serial.printf("Energy data sent: %.2fkWh\n", totalEnergy);

//     // Cập nhật trạng thái gửi dữ liệu
//     if (Firebase.setInt(firebaseData, statusPath, 1)) {
//       Serial.println("Marked today's energy data as sent.");
//     } else {
//       Serial.printf("Failed to mark today's energy status: %s\n", firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.printf("Failed to send energy data: %s\n", firebaseData.errorReason().c_str());
//   }
// }

// void sendEnergyDataOncePerDay() {
//   struct tm timeinfo;
//   if (!getLocalTime(&timeinfo)) {
//     Serial.println("Failed to get local time.");
//     return;
//   }

//   // Lấy ngày hiện tại
//   char currentDate[11];
//   snprintf(currentDate, sizeof(currentDate), "%04d-%02d-%02d",
//            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
//   String statusPath = String("/Energy_use_status/") + String(currentDate);

//   // Kiểm tra trạng thái gửi trên Firebase
//   if (Firebase.getInt(firebaseData, statusPath)) {
//     if (firebaseData.intData() == 1) {
//       Serial.println("Today's energy data already sent. Skipping...");
//       return;  // Thoát nếu trạng thái là `1`
//     }
//   } else if (firebaseData.httpCode() != FIREBASE_ERROR_HTTP_CODE_NOT_FOUND) {
//     // Nếu lỗi khác 404 (trạng thái không tồn tại), báo lỗi và thoát
//     Serial.printf("Failed to check energy status: %s\n", firebaseData.errorReason().c_str());
//     return;
//   }

//   // Nếu trạng thái chưa được ghi nhận, tiến hành gửi dữ liệu
//   float totalEnergy = pzem.energy();
//   if (isnan(totalEnergy)) totalEnergy = 0;  // Đảm bảo không gửi giá trị NaN

//   char currentTimeStr[20];
//   snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d_%02d:%02d:%02d",
//            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
//            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

//   String energyPath = String("/Energy_use/") + String(currentTimeStr) + "/totalEnergy";

//   if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
//     Serial.printf("Energy data sent: %.2fkWh\n", totalEnergy);

//     // Cập nhật trạng thái gửi dữ liệu trong ngày trên Firebase
//     if (Firebase.setInt(firebaseData, statusPath, 1)) {
//       Serial.println("Marked today's energy data as sent.");
//     } else {
//       Serial.printf("Failed to mark today's energy status: %s\n", firebaseData.errorReason().c_str());
//     }
//   } else {
//     Serial.printf("Failed to send energy data: %s\n", firebaseData.errorReason().c_str());
//   }
// }