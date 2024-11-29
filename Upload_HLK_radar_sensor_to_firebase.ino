// void TaskUploadHLK(void* pvParameters) {
//   pinMode(OUT_PIN, INPUT_PULLDOWN);
//   while(1) {
//     // Lấy quyền semaphore để truy cập tài nguyên chung
//       if (xSemaphoreTake(sensorHLKSemaphore, (TickType_t)10) == pdTRUE) {
//         // ----- ĐỌC VÀ GỬI TRẠNG THÁI RADAR -----
//             int radarStatus = digitalRead(OUT_PIN);
//             Serial.printf("Radar status read from OUT_PIN: %d\n", radarStatus);
//             // Gửi trực tiếp giá trị radarStatus tới Firebase
//             if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
//                 Serial.printf("Radar state updated successfully: %d\n", radarStatus);
//             } else {
//                 Serial.println(firebaseData.errorReason());
//             }
//              // Trả quyền semaphore
//             xSemaphoreGive(sensorHLKSemaphore);
//       }
//       vTaskDelay(10000 / portTICK_PERIOD_MS); // Nhả CPU trong 10s
//   }
// }

// void TaskUploadHLK(void* pvParameters) {
//   // Cấu hình pin radar
//   pinMode(OUT_PIN, INPUT_PULLDOWN);
//   TickType_t lastWakeTime = xTaskGetTickCount();
//   const TickType_t delayTicks = pdMS_TO_TICKS(15000);  // 1000 ms = 1 giây

//   Preferences preferences;                                  // Khởi tạo Preferences để lưu trạng thái `lastSentDay`
//   preferences.begin("energy-data", false);                  // Namespace "energy-data"
//   int lastSentDay = preferences.getInt("lastSentDay", -1);  // Lấy ngày đã gửi lần trước

//   while (1) {
//     // ----- ĐỌC VÀ GỬI TRẠNG THÁI RADAR -----
//     int radarStatus = digitalRead(OUT_PIN);
//     Serial.printf("Radar status read from OUT_PIN: %d\n", radarStatus);

//     // Gửi trực tiếp giá trị radarStatus tới Firebase
//     if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
//       Serial.printf("Radar state updated successfully: %d\n", radarStatus);
//     } else {
//       Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
//     }

//     // ----- ĐỌC VÀ GỬI NHIỆT ĐỘ -----
//     sensors1.requestTemperatures();
//     sensors2.requestTemperatures();
//     sensors3.requestTemperatures();
//     sensors4.requestTemperatures();

//     float temp1 = sensors1.getTempCByIndex(0);
//     float temp2 = sensors2.getTempCByIndex(0);
//     float temp3 = sensors3.getTempCByIndex(0);
//     float temp4 = sensors4.getTempCByIndex(0);

//     Serial.printf("Temperature of computer 1, 2, 3, 4: %.2f, %.2f, %.2f, %.2f\n", temp1, temp2, temp3, temp4);
//     // Gửi dữ liệu trực tiếp từng cảm biến lên Firebase
//     if (Firebase.setFloat(firebaseData, "/Temperatures/computer1/temperature", temp1)) {
//       Serial.println("Temperature of computer 1 sent successfully.");
//     } else {
//       Serial.println(firebaseData.errorReason());
//     }
//     if (Firebase.setFloat(firebaseData, "/Temperatures/computer2/temperature", temp2)) {
//       Serial.println("Temperature of computer 2 sent successfully.");
//     } else {
//       Serial.println(firebaseData.errorReason());
//     }
//     if (Firebase.setFloat(firebaseData, "/Temperatures/computer3/temperature", temp3)) {
//       Serial.println("Temperature of computer 3 sent successfully.");
//     } else {
//       Serial.println(firebaseData.errorReason());
//     }
//     if (Firebase.setFloat(firebaseData, "/Temperatures/computer4/temperature", temp4)) {
//       Serial.println("Temperature of computer 4 sent successfully.");
//     } else {
//       Serial.println(firebaseData.errorReason());
//     }

//     // ----- ĐỌC VÀ GỬI DỮ LIỆU PZEM -----
//     float voltage = pzem.voltage();
//     float current = pzem.current();
//     float frequency = pzem.frequency();
//     float power = pzem.power();
//     float powerFactor = pzem.pf();
//     float energy = pzem.energy();

//     if (isnan(voltage)) voltage = 0;
//     if (isnan(current)) current = 0;
//     if (isnan(power)) power = 0;
//     if (isnan(frequency)) frequency = 0;
//     if (isnan(powerFactor)) powerFactor = 0;
//     if (isnan(energy)) energy = 0;

//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);

//     // ----- GỬI DỮ LIỆU totalEnergy MỘT LẦN MỖI NGÀY -----
//     struct tm timeinfo;
//     if (getLocalTime(&timeinfo)) {
//       int currentDay = timeinfo.tm_yday;  // Lấy ngày hiện tại

//       if (currentDay != lastSentDay) {
//         lastSentDay = currentDay;                        // Cập nhật ngày đã gửi
//         preferences.putInt("lastSentDay", lastSentDay);  // Ghi vào bộ nhớ

//         String currentTime = getCurrentTime();
//         String energyPath = "/Energy_use/" + currentTime + "/totalEnergy";

//         if (Firebase.setFloat(firebaseData, energyPath, energy)) {
//           Serial.printf("Total Energy sent: %.2f kWh\n", energy / 1000);
//         } else {
//           Serial.printf("Failed to send total energy: %s\n", firebaseData.errorReason().c_str());
//         }
//       } else {
//         Serial.println("Total Energy already sent for today. Skipping...");
//       }
//     } else {
//       Serial.println("Failed to get current time for totalEnergy update.");
//     }

//     // Nhả CPU trong 10s
//     // vTaskDelay(15000 / portTICK_PERIOD_MS);
//     preferences.end();
//     vTaskDelayUntil(&lastWakeTime, delayTicks);
//   }
// }

// code ok nhưng còn dùng nhiều stack quá
// Khởi tạo preferencesDay để lưu trạng thái `lastSentDay`
// Preferences preferencesDay;
// void TaskUploadHLK(void* pvParameters) {
//   TickType_t lastWakeTime = xTaskGetTickCount();       // Lưu thời gian bắt đầu task
//   const TickType_t delayTicks = pdMS_TO_TICKS(20000);  // Chu kỳ thực thi: 15 giây
//   // Cấu hình pin radar
//   pinMode(OUT_PIN, INPUT_PULLDOWN);
//   preferencesDay.begin("energy-data", false);                  // Namespace "energy-data"
//   int lastSentDay = preferencesDay.getInt("lastSentDay", -1);  // Lấy ngày đã gửi lần trước
//   while (1) {
//     // ----- ĐỌC VÀ GỬI TRẠNG THÁI RADAR -----
//     int radarStatus = digitalRead(OUT_PIN);
//     if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
//       Serial.printf("Đã trạng thái cảm biến radar: %d\n", radarStatus);
//     } else {
//       Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
//     }
//     // ----- ĐỌC VÀ GỬI NHIỆT ĐỘ -----
//     sensors1.requestTemperatures();
//     sensors2.requestTemperatures();
//     sensors3.requestTemperatures();
//     sensors4.requestTemperatures();

//     float temps[4] = {
//       sensors1.getTempCByIndex(0),
//       sensors2.getTempCByIndex(0),
//       sensors3.getTempCByIndex(0),
//       sensors4.getTempCByIndex(0)
//     };
//     for (int i = 0; i < 4; i++) {
//       String tempPath = "/Temperatures/computer" + String(i + 1) + "/temperature";
//       if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
//         Serial.printf("Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
//       } else {
//         Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
//       }
//     }

//     // ----- ĐỌC VÀ GỬI DỮ LIỆU PZEM -----
//     float voltage = pzem.voltage();
//     float current = pzem.current();
//     float frequency = pzem.frequency();
//     float power = pzem.power();
//     float powerFactor = pzem.pf();
//     float energy = pzem.energy();

//     if (isnan(voltage)) voltage = 0;
//     if (isnan(current)) current = 0;
//     if (isnan(power)) power = 0;
//     if (isnan(frequency)) frequency = 0;
//     if (isnan(powerFactor)) powerFactor = 0;
//     if (isnan(energy)) energy = 0;

//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
//     Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);
//     Serial.printf("\nĐã gửi dữ liệu voltage,current, frequency,power ");

//     // if (!isnan(voltage)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
//     // if (!isnan(current)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
//     // if (!isnan(frequency)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
//     // if (!isnan(power)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);

//     // ----- GỬI `totalEnergy` MỘT LẦN MỖI NGÀY -----
//     struct tm timeinfo;
//     if (getLocalTime(&timeinfo)) {
//       int currentDay = timeinfo.tm_yday;  // Lấy ngày hiện tại

//       if (currentDay != lastSentDay) {
//         lastSentDay = currentDay;
//         preferences.putInt("lastSentDay", lastSentDay);  // Lưu vào non-volatile

//         String energyPath = "/Energy_use/" + String(currentDay) + "/totalEnergy";
//         if (Firebase.setFloat(firebaseData, energyPath, energy)) {
//           Serial.printf("Total Energy sent: %.2f kWh\n", energy / 1000);
//         } else {
//           Serial.printf("Failed to send total energy: %s\n", firebaseData.errorReason().c_str());
//         }
//       } else {
//         Serial.println(". Đã gửi dữ liệu điện tích lũy ngày hôm nay rồi");
//       }
//     } else {
//       Serial.println("Failed to get current time for totalEnergy update.");
//     }
//     vTaskDelayUntil(&lastWakeTime, delayTicks); // Thực thi lại theo chu kỳ cố định tạo delay 20s
//   }
//   // Đóng Preferences trước khi kết thúc task
//   preferences.end();
// }


// code mới code ok của ngày hôm nay
void TaskUploadHLK(void* pvParameters) {
  // Cấu hình pin radar
  // pinMode(OUT_PIN, INPUT_PULLDOWN);

  TickType_t lastWakeTime = xTaskGetTickCount();       // Lưu thời gian bắt đầu task
  const TickType_t delayTicks = pdMS_TO_TICKS(25000);  // Chu kỳ thực thi: 20 giây

  // Mở Preferences để lưu trạng thái `lastSentDay`
  Preferences preferencesDay;
  preferencesDay.begin("energy-data", false);

  // Lấy thông tin ngày lưu trữ lần trước
  int lastSentDay = preferencesDay.getInt("lastSentDay", -1);
  int lastSentMonth = preferencesDay.getInt("lastSentMonth", -1);
  int lastSentYear = preferencesDay.getInt("lastSentYear", -1);
  // biến này để test chức năng gửi theo ngày
  // lastSentDay = lastSentDay -2 ;
  // preferencesDay.putInt("lastSentDay", lastSentDay);
  // Serial.println("in ngay thang nam lan trước");
  // Serial.println(lastSentDay);
  // Serial.println(lastSentMonth);
  // Serial.println(lastSentYear);

  // ----- GỬI `totalEnergy` MỘT LẦN MỖI NGÀY -----
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int currentDay = timeinfo.tm_mday;          // Lấy ngày hiện tại
    int currentMonth = timeinfo.tm_mon + 1;     // Lấy tháng hiện tại (1-12)
    int currentYear = timeinfo.tm_year + 1900;  // Lấy năm hiện tại (tính từ 1900)
    // Serial.println("in ngay thang nam lan trước");
    // Serial.println(lastSentDay);
    // Serial.println(lastSentMonth);
    // Serial.println(lastSentYear);
    // Serial.println("in ngay thang nam hom nay");
    // Serial.println(currentDay);
    // Serial.println(currentMonth);
    // Serial.println(currentYear);

    // So sánh ngày, tháng, năm
    if (currentDay != lastSentDay || currentMonth != lastSentMonth || currentYear != lastSentYear) {
      // Nếu là ngày mới hoặc năm mới
      if (currentYear > lastSentYear) {
        // Reset năng lượng PZEM nếu là năm mới
        pzem.resetEnergy();
        Serial.println("Reset PZEM energy data for the new year.");
      }

      // Cập nhật ngày, tháng, năm đã gửi
      lastSentDay = currentDay;
      lastSentMonth = currentMonth;
      lastSentYear = currentYear;

      preferencesDay.putInt("lastSentDay", lastSentDay);
      preferencesDay.putInt("lastSentMonth", lastSentMonth);
      preferencesDay.putInt("lastSentYear", lastSentYear);

      // Gửi dữ liệu năng lượng tích lũy
      float totalEnergy = pzem.energy();
      if (isnan(totalEnergy)) totalEnergy = 0;

      if (!isnan(totalEnergy)  ) {
        String currentTime = getCurrentTime();
        String energyPath = "/Energy_use/" + currentTime + "/totalEnergy";
        if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
          Serial.printf("Total Energy sent: %.2f kWh\n", totalEnergy);
        } else {
          Serial.printf("Failed to send total energy: %s\n", firebaseData.errorReason().c_str());
        }
      }
    } else {
      Serial.println("Total Energy already sent for today. Skipping...");
    }
  } else {
    Serial.println("Failed to get current time for totalEnergy update.");
  }
  preferencesDay.end();


  while (1) {
    Serial.print("sensor core: ");
    Serial.println(xPortGetCoreID());
    // ----- ĐỌC VÀ GỬI TRẠNG THÁI RADAR -----
    handleRadarData();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ----- ĐỌC VÀ GỬI NHIỆT ĐỘ -----
    handleTemperatureData();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    // ----- ĐỌC VÀ GỬI DỮ LIỆU PZEM -----
    handlePZEMData();
    vTaskDelay(2000 / portTICK_PERIOD_MS);


    // Thực thi lại theo chu kỳ cố định
    vTaskDelayUntil(&lastWakeTime, delayTicks);
  }

  // Đóng Preferences trước khi kết thúc task
  // preferencesDay.end();
}

// Hàm xử lý dữ liệu radar
void handleRadarData() {
  int radarStatus = digitalRead(OUT_PIN);
  if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
    Serial.printf("Radar state updated successfully: %d\n", radarStatus);
  } else {
    Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
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

  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      String tempPath = "/Temperatures/computer" + String(i + 1) + "/temperature";
      if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
        Serial.printf("Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
      } else {
        Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
      }
    }
  }
}

// Hàm xử lý dữ liệu PZEM
void handlePZEMData() {
  float voltage = pzem.voltage();
  float current = pzem.current();
  float frequency = pzem.frequency();
  float power = pzem.power();
  float energy = pzem.energy();
  if (!isnan(voltage)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
  if (!isnan(current)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
  if (!isnan(frequency)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
  if (!isnan(power)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);
  if (!isnan(energy)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/energy", energy);

  Serial.printf("PZEM Data sent: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n", voltage, current, frequency, power, energy);
}


// code mới 28/11 chia ra 3 task nhưng chạy bị lỗi--------------------------------------------------------------------------------------------------

// #define DELAY_RADAR 10000  // Chu kỳ thực thi radar (20 giây)
// // Task 1: Gửi dữ liệu từ cảm biến radar
// void TaskRadarData(void* pvParameters) {
//   pinMode(OUT_PIN, INPUT_PULLDOWN);
//   while (1) {
//     int radarStatus = digitalRead(OUT_PIN);
//     if (Firebase.setInt(firebaseData, "/HLK_RADAR/status", radarStatus)) {
//       Serial.printf("Radar state updated successfully: %d\n", radarStatus);
//     } else {
//       Serial.printf("Failed to update radar state: %s\n", firebaseData.errorReason().c_str());
//     }
//     vTaskDelay(pdMS_TO_TICKS(DELAY_RADAR));
//   }
// }

// #define DELAY_TEMPERATURE 15000  // Chu kỳ thực thi nhiệt độ (20 giây)
// // Task 2: Gửi dữ liệu nhiệt độ
// void TaskTemperatureData(void* pvParameters) {
//   while (1) {
//     sensors1.requestTemperatures();
//     sensors2.requestTemperatures();
//     sensors3.requestTemperatures();
//     sensors4.requestTemperatures();

//     float temps[4] = {
//       sensors1.getTempCByIndex(0),
//       sensors2.getTempCByIndex(0),
//       sensors3.getTempCByIndex(0),
//       sensors4.getTempCByIndex(0)
//     };

//     for (int i = 0; i < 4; i++) {
//       if (!isnan(temps[i])) {
//         String tempPath = "/Temperatures/computer" + String(i + 1) + "/temperature";
//         if (Firebase.setFloat(firebaseData, tempPath, temps[i])) {
//           Serial.printf("Temperature of computer %d sent successfully: %.2f\n", i + 1, temps[i]);
//         } else {
//           Serial.printf("Failed to send temperature of computer %d: %s\n", i + 1, firebaseData.errorReason().c_str());
//         }
//       }
//     }
//     vTaskDelay(pdMS_TO_TICKS(DELAY_TEMPERATURE));
//   }
// }

// #define DELAY_PZEM 20000  // Chu kỳ thực thi PZEM (20 giây)
// // Task 3: Gửi dữ liệu PZEM và năng lượng tích lũy hàng ngày
// void TaskPZEMData(void* pvParameters) {
//   Preferences preferencesDay;
//   preferencesDay.begin("energy-data", false);
//   int lastSentDay = preferencesDay.getInt("lastSentDay", -1);

//   TickType_t lastWakeTime = xTaskGetTickCount();       // Lưu thời gian bắt đầu task
//   const TickType_t delayTicks = pdMS_TO_TICKS(25000);  // Chu kỳ thực thi: 20 giây

//   // ----- GỬI `totalEnergy` MỘT LẦN MỖI NGÀY -----
//   struct tm timeinfo;
//   if (getLocalTime(&timeinfo)) {
//     int currentDay = timeinfo.tm_yday;  // Lấy ngày hiện tại
//     if (currentDay != lastSentDay) {
//       lastSentDay = currentDay;

//       preferencesDay.putInt("lastSentDay", lastSentDay);  // Lưu vào bộ nhớ

//       // Gửi dữ liệu năng lượng tích lũy
//       float totalEnergy = pzem.energy();
//       String currentTime = getCurrentTime();
//       if (!isnan(totalEnergy)) {
//         String energyPath = "/Energy_use/" + currentTime + "/totalEnergy";
//         if (Firebase.setFloat(firebaseData, energyPath, totalEnergy)) {
//           Serial.printf("Total Energy sent: %.2f kWh\n", totalEnergy * 1000);
//         } else {
//           Serial.printf("Failed to send total energy: %s\n", firebaseData.errorReason().c_str());
//         }
//       }
//     } else {
//       Serial.println("Total Energy already sent for today. Skipping...");
//     }
//   } else {
//     Serial.println("Failed to get current time for totalEnergy update.");
//   }

//   while (1) {
//     float voltage = pzem.voltage();
//     float current = pzem.current();
//     float frequency = pzem.frequency();
//     float power = pzem.power();
//     float energy = pzem.energy();

//     if (!isnan(voltage)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/voltage", voltage);
//     if (!isnan(current)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/current", current);
//     if (!isnan(frequency)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/frequency", frequency);
//     if (!isnan(power)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/power", power);
//     if (!isnan(energy)) Firebase.setFloat(firebaseData, "/PZEM_Voltage/energy", energy);

//     Serial.printf("PZEM Data sent: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
//                   voltage, current, frequency, power, energy);
//     vTaskDelay(pdMS_TO_TICKS(DELAY_PZEM));
//   }
// }

// ------------------------------------------------------------------------------------------------------------------------------------------------------
