// code mới code ok của ngày hôm nay
void TaskUploadHLK(void* pvParameters) {


  // // Gửi lần đầu tiên cho dữ liệu Energy mỗi ngày
  // TickType_t lastWakeTime = xTaskGetTickCount();       // Lưu thời gian bắt đầu task
  // const TickType_t delayTicks = pdMS_TO_TICKS(25000);  // Chu kỳ thực thi: 25 giây
  // Preferences preferencesDay;  // Mở Preferences để lưu trạng thái `lastSentDay`
  // preferencesDay.begin("energy-data", false);

  // // Lấy thông tin ngày lưu trữ lần trước
  // int lastSentDay = preferencesDay.getInt("lastSentDay", -1);
  // int lastSentMonth = preferencesDay.getInt("lastSentMonth", -1);
  // int lastSentYear = preferencesDay.getInt("lastSentYear", -1);

  // // ----- GỬI `totalEnergy` MỘT LẦN MỖI NGÀY -----
  // struct tm timeinfo;
  // if (getLocalTime(&timeinfo)) {
  //   int currentDay = timeinfo.tm_mday;          // Lấy ngày hiện tại
  //   int currentMonth = timeinfo.tm_mon + 1;     // Lấy tháng hiện tại (1-12)
  //   int currentYear = timeinfo.tm_year + 1900;  // Lấy năm hiện tại (tính từ 1900)

  //   // So sánh ngày, tháng, năm
  //   if (currentDay != lastSentDay || currentMonth != lastSentMonth || currentYear != lastSentYear) {
  //     // Nếu là ngày mới hoặc năm mới
  //     if (currentYear > lastSentYear) {
  //       // Reset năng lượng PZEM nếu là năm mới
  //       pzem.resetEnergy();
  //       Serial.println("Reset PZEM energy data for the new year.");
  //     }
  //     // Cập nhật ngày, tháng, năm đã gửi
  //     lastSentDay = currentDay;
  //     lastSentMonth = currentMonth;
  //     lastSentYear = currentYear;

  //     preferencesDay.putInt("lastSentDay", lastSentDay);
  //     preferencesDay.putInt("lastSentMonth", lastSentMonth);
  //     preferencesDay.putInt("lastSentYear", lastSentYear);

  //     // Gửi dữ liệu năng lượng tích lũy
  //     float totalEnergy = pzem.energy();
  //     if (isnan(totalEnergy)) totalEnergy = 0;
  //     if (!isnan(totalEnergy)) {
  //       String currentTime = getCurrentTime();
  //       String energyPath = "/Energy_use/" + currentTime + "/totalEnergy";
  //       if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
  //         Serial.printf("Total Energy sent: %.2f kWh\n", totalEnergy);
  //       } else {
  //         Serial.printf("Failed to send total energy: %s\n", firebaseData.errorReason().c_str());
  //       }
  //     }
  //   } else {
  //     Serial.println("Total Energy already sent for today. Skipping...");
  //   }
  // } else {
  //   Serial.println("Failed to get current time for totalEnergy update.");
  // }
  // preferencesDay.end();

  for (;;) {
    checkWiFiConnection();  // Gọi hàm kiểm tra và kết nối lại Wi-Fi nếu cần
    if (xSemaphoreTake(relayMutex, portMAX_DELAY)) {
      Serial.print("sensor core: ");
      Serial.println(xPortGetCoreID());

      // Gửi dữ liệu năng lượng một lần mỗi ngày
      sendEnergyDataOncePerDay();


      // ----- ĐỌC VÀ GỬI TRẠNG THÁI RADAR -----
      handleRadarData();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      // ----- ĐỌC VÀ GỬI NHIỆT ĐỘ -----
      handleTemperatureData();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      // ----- ĐỌC VÀ GỬI DỮ LIỆU PZEM -----
      handlePZEMData();
      vTaskDelay(500 / portTICK_PERIOD_MS);
      Serial.println("2----> Task Sensor Update: Accessing shared upload sensor.");
      xSemaphoreGive(relayMutex);
    } else {
      Serial.println("Failed to acquire relayMutex for upload from Sensor update firebase. ");
    }
    vTaskDelay(20000 / portTICK_PERIOD_MS);
  }

  // while (1) {
  //   // Đồng bộ Firebase với mutex
  //   if (xSemaphoreTake(firebaseMutex, portMAX_DELAY)) {
  //     handleRadarData();
  //     vTaskDelay(500 / portTICK_PERIOD_MS);
  //     handleTemperatureData();
  //     vTaskDelay(500 / portTICK_PERIOD_MS);
  //     handlePZEMData();
  //     vTaskDelay(500 / portTICK_PERIOD_MS);
  //     xSemaphoreGive(firebaseMutex);
  //   }

  //   vTaskDelayUntil(&lastWakeTime, delayTicks);
  // }
}



// Hàm xử lý dữ liệu radar
void handleRadarData() {
  int radarStatus = digitalRead(OUT_PIN);
  checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi

  if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
    Serial.printf("Updated Radar state successfully: %d\n", radarStatus);
  } else {
    // Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
    Serial.printf("Failed to update radar state: bug is function handleRadarData");
  }
}

// Hàm xử lý dữ liệu nhiệt độ
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

  checkWiFiConnection();  // Kiểm tra kết nối Wi-Fi

  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      String tempPath = "/Temperatures/computer" + String(i + 1) + "/temperature";
      if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
        Serial.printf("Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
      } else {
        // Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
        Serial.printf("Failed to send temperature of computer: function handleTemperatureData");
      }
    }
  }
}

// Hàm xử lý dữ liệu PZEM
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

void handlePZEMData() {
  // Đọc dữ liệu từ PZEM
  float voltage = pzem.voltage();
  float current = pzem.current();
  float frequency = pzem.frequency();
  float power = pzem.power();
  float energy = pzem.energy();

  // Kiểm tra kết nối Wi-Fi
  checkWiFiConnection();

  // Gửi dữ liệu lên Firebase
  if (!isnan(voltage)) {
    if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage)) {
      Serial.printf("Voltage sent: %.2fV\n", voltage);
    } else {
      Serial.printf("Failed to send voltage: %.2fV, Error: %s\n", voltage, firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("Voltage data invalid (NaN).");
  }

  if (!isnan(current)) {
    if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current)) {
      Serial.printf("Current sent: %.2fA\n", current);
    } else {
      Serial.printf("Failed to send current: %.2fA, Error: %s\n", current, firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("Current data invalid (NaN).");
  }

  if (!isnan(frequency)) {
    if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency)) {
      Serial.printf("Frequency sent: %.2fHz\n", frequency);
    } else {
      Serial.printf("Failed to send frequency: %.2fHz, Error: %s\n", frequency, firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("Frequency data invalid (NaN).");
  }

  if (!isnan(power)) {
    if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power)) {
      Serial.printf("Power sent: %.2fW\n", power);
    } else {
      Serial.printf("Failed to send power: %.2fW, Error: %s\n", power, firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("Power data invalid (NaN).");
  }

  if (!isnan(energy)) {
    if (Firebase.setFloat(firebaseData, "/PZEM_Voltage/energy", energy)) {
      Serial.printf("Energy sent: %.2fkWh\n", energy);
    } else {
      Serial.printf("Failed to send energy: %.2fkWh, Error: %s\n", energy, firebaseData.errorReason().c_str());
    }
  } else {
    Serial.println("Energy data invalid (NaN).");
  }

  // Tổng hợp log
  Serial.printf("PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
                voltage, current, frequency, power, energy);
}

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

void sendEnergyDataOncePerDay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get local time.");
  }

  // Lấy ngày hiện tại
  char currentDate[11];
  snprintf(currentDate, sizeof(currentDate), "%04d-%02d-%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  String statusPath = String("/Energy_use_status/") + String(currentDate);

  // Kiểm tra trạng thái gửi trên Firebase
  if (Firebase.getInt(firebaseData, statusPath)) {
    if (firebaseData.intData() == 1) {
      // Trạng thái đã được đặt là 1, không cần gửi lại
      Serial.println("Today's energy data already sent. Skipping...");
      return;
    }
  }
  // if (firebaseData.httpCode() == FIREBASE_ERROR_HTTP_CODE_NOT_FOUND)
  else {
    // Trạng thái chưa tồn tại, tiến hành gửi dữ liệu
    Serial.println("Status not found. Sending energy data...");

    // Gửi dữ liệu năng lượng
    float totalEnergy = pzem.energy();
    if (isnan(totalEnergy)) totalEnergy = 0;  // Đảm bảo không gửi giá trị NaN

    char currentTimeStr[20];
    snprintf(currentTimeStr, sizeof(currentTimeStr), "%04d-%02d-%02d_%02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    String energyPath = String("/Energy_use/") + String(currentTimeStr) + "/totalEnergy";

    if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
      Serial.printf("Energy data sent: %.2fkWh\n", totalEnergy);

      // Sau khi gửi thành công, cập nhật trạng thái
      if (Firebase.setInt(firebaseData, statusPath, 1)) {
        Serial.println("Marked today's energy data as sent.");
      } else {
        Serial.printf("Failed to mark today's energy status: %s\n", firebaseData.errorReason().c_str());
      }
    } else {
      Serial.printf("Failed to send energy data: %s\n", firebaseData.errorReason().c_str());
    }
  }
  // else {
  //   // Báo lỗi khác khi kiểm tra trạng thái
  //   Serial.printf("Failed to check energy status: %s\n", firebaseData.errorReason().c_str());
  // }
}
