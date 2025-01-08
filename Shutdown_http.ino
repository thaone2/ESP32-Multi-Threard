void shutdownServer(const char* serverIP, int relayIndex) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String serverPath = String("http://") + serverIP + ":" + serverPort + "/shutdown";
    int attempt = 0;       // Biến đếm số lần thử
    bool success = false;  // Biến theo dõi trạng thái thành công
    while (attempt < 1 && !success) {
      Serial.print("Attempt ");
      Serial.print(attempt + 1);
      Serial.println(" to shutdown server...");
      http.begin(serverPath.c_str());
      int httpResponseCode = http.GET();
      if (httpResponseCode > 0) {
        Serial.print("HTTP Response code from ");
        Serial.print(serverIP);
        Serial.print(": ");
        Serial.println(httpResponseCode);
        if (httpResponseCode == 200) {
          success = true;
          Serial.println("Shutdown command sent successfully.");
        } else {
          Serial.println("Shutdown command failed. Retrying...");
        }
      } else {
        Serial.print("Error code from ");
        Serial.print(serverIP);
        Serial.print(": ");
        Serial.println(httpResponseCode);
      }
      http.end();
      attempt++;
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    if (!success) {
      Serial.println("Failed to shutdown the server");
    }
  } else {
    Serial.println("WiFi not connected.");
  }
}