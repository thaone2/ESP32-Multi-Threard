// Hàm shutdown máy tính qua HTTP request, thử tối đa 3 lần nếu không thành công
void shutdownServer(const char* serverIP, int relayIndex) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String serverPath = String("http://") + serverIP + ":" + serverPort + "/shutdown";
        int attempt = 0;   // Biến đếm số lần thử
        bool success = false; // Biến theo dõi trạng thái thành công
        // Thử gửi yêu cầu tắt máy tối đa 2 lần
        while (attempt < 1 && !success) {
            Serial.print("Attempt ");
            Serial.print(attempt + 1);
            Serial.println(" to shutdown server...");
            http.begin(serverPath.c_str());
            int httpResponseCode = http.GET();
            if (httpResponseCode > 0) {  
                // Kiểm tra phản hồi thành công
                Serial.print("HTTP Response code from ");
                Serial.print(serverIP);
                Serial.print(": ");
                Serial.println(httpResponseCode);
                // Kiểm tra mã phản hồi từ server, ví dụ mã 200 là thành công
                if (httpResponseCode == 200) {
                    success = true; // Nếu phản hồi thành công, đánh dấu hoàn thành
                    Serial.println("Shutdown command sent successfully.");
                } else {
                    Serial.println("Shutdown command failed. Retrying...");
                }
            } else {
                // Nếu kết nối HTTP thất bại, in lỗi
                Serial.print("Error code from ");
                Serial.print(serverIP);
                Serial.print(": ");
                Serial.println(httpResponseCode);
                Serial.println("Retrying...");
            }
            http.end();  // Kết thúc kết nối
            attempt++;   // Tăng biến đếm số lần thử
            vTaskDelay(500/ portTICK_PERIOD_MS);
        }
        // Nếu sau 3 lần vẫn không thành công
        if (!success) {
            Serial.println("Failed to shutdown the server after 3 attempts.");
        }
    } else {
        Serial.println("WiFi not connected.");
    }
}