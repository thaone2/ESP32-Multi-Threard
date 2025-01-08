// 2.1 Task cập nhật dữ liệu các cảm biến
void TaskUploadAllSensors(void* pvParameters) {
  for (;;) {
    // checkWiFiConnection();
    if (xSemaphoreTake(relayMutex, portMAX_DELAY) == pdTRUE) {
      // Serial.print("2.1: sensor core: ");
      // Serial.println(xPortGetCoreID());
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
    vTaskDelay(25000 / portTICK_PERIOD_MS);
  }
}


// 2.2 Hàm xử lý dữ liệu radar
void handleRadarData() {
  int radarStatus = digitalRead(OUT_PIN);
  checkWiFiConnection();
  if (Database.set<int>(aClient, "/HLK_RADAR/status", radarStatus)) {
    Serial.printf("2.2 Updated Radar state successfully: %d\n", radarStatus);
  } else {
    Serial.printf("2.2 Failed to update radar state: %s\n", aClient.lastError().message().c_str());
  }
}

// 2.3 Hàm xử lý dữ liệu nhiệt độ
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

  Serial.printf("2.3 Temperature values:  %.2f %.2f %.2f %.2f \n ", temps[0],temps[1],temps[2],temps[3] );
  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      float roundedTemp = round(temps[i] * 100) / 100.0;
      String temperaturePath = "/Temperatures/computer" + String(i + 1) + "/temperature";

      // Serial.printf("2.3 Updating %s with value %.2f... ", temperaturePath.c_str(), roundedTemp);
      bool statusUpdate = Database.set<float>(aClient, temperaturePath, roundedTemp);
      if (statusUpdate) {
        // Serial.println("Success");
      } else {
        Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
      }
    } else {
      Serial.printf("2.3 Temperature sensor %d returned an invalid value\n", i + 1);
    }
  }
}

// 2.4v2 Hàm xử lý dữ liệu PZEM004T
void handlePZEMData() {
  float voltage = pzem.voltage();
  float current = pzem.current();
  float frequency = pzem.frequency();
  float power = pzem.power();
  float energy = pzem.energy();

  voltage = isnan(voltage) ? 0 : round(voltage * 100) / 100.0;
  current = isnan(current) ? 0 : round(current * 100) / 100.0;
  frequency = isnan(frequency) ? 0 : round(frequency * 100) / 100.0;
  power = isnan(power) ? 0 : round(power * 100) / 100.0;
  energy = isnan(energy) ? 0 : round(energy * 100) / 100.0;

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
    // Serial.printf("2.4 Updating %s with value %.2f... ", path.c_str(), data[i].value);
    bool statusUpdate = Database.set<float>(aClient, path, data[i].value);
    if (statusUpdate) {
      // Serial.println("Success");
    } else {
      Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
    }
  }

  // Tổng hợp log
  Serial.printf("2.4 PZEM Data processed: Voltage=%.2fV, Current=%.2fA, Frequency=%.2fHz, Power=%.2fW, Energy=%.2fkWh\n",
                voltage, current, frequency, power, energy);
}

// 2.5 Hàm kiểm tra và gửi năng lượng một lần mỗi ngày
void sendEnergyDataOncePerDay() {
  String currentDate = getCurrentDate();
  String statusPath = "/Energy_usage_status/" + currentDate;

  int status = Database.get<int>(aClient, statusPath);
  if (aClient.lastError().code() == 0) {
    if (status == 1) {
      Serial.println("2.5 Today's energy data already sent. Skipping...");
      return;
    }
  }
  Serial.println("2.5 Status not found or not sent. Sending energy data...");

  // Reset relay count mỗi ngày
  for (int i = 0; i < 4; i++) {
    relayOnCounts[i] = 0;
  }
  Serial.println("2.5 Relay counts reset!");

  float totalEnergy = pzem.energy();
  if (isnan(totalEnergy)) totalEnergy = 0;
  String currentTimeStr = getCurrentTime();
  String energyPath = "/Energy_use/" + currentTimeStr + "/totalEnergy";

  if (Database.set<float>(aClient, energyPath, totalEnergy)) {
    Serial.printf("2.5 Energy data sent: %.2fkWh\n", totalEnergy);
    if (Database.set<int>(aClient, statusPath, 1)) {
      Serial.println("2.5 Marked today's energy data as sent.");
    } else {
      Serial.printf("2.5 Failed to mark today's energy status: %s\n", aClient.lastError().message().c_str());
    }
  } else {
    Serial.printf("2.5 Failed to send energy data: %s\n", aClient.lastError().message().c_str());
  }
}

//2.6v2  Hàm gửi dữ liệu nhiệt độ mỗi 30 phút và gửi một lần qua JSON
void sendTemperatureAfter30Minutes() {
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
  // Serial.printf("2.6 Temperature values:  %.2f %.2f %.2f %.2f \n ", temps[0],temps[1],temps[2],temps[3] );

  String basePath = "/Temperatures_30/" + currentTimeStr;

  for (int i = 0; i < 4; i++) {
    if (!isnan(temps[i])) {
      float roundedTemp = round(temps[i] * 100) / 100.0;
      String path = basePath + "/computer" + String(i + 1) + "/temperature";
      // Serial.printf("2.6 Updating %s with value %.2f... ", path.c_str(), roundedTemp);
      bool statusUpdate = Database.set<float>(aClient, path, roundedTemp);
      if (statusUpdate) {
        // Serial.println("Success");
      } else {
        Serial.printf("Failed: %s\n", aClient.lastError().message().c_str());
      }
    } else {
      Serial.printf("2.6 Temperature for computer%d is invalid (NaN).\n", i + 1);
    }
  }
}
