#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/semphr.h>

// Save a reference to the REAL HardwareSerial BEFORE the macro
extern HardwareSerial& HWSerial;

class LogBuffer : public Print {
private:
    static const size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    volatile size_t head = 0;
    volatile size_t tail = 0;
    SemaphoreHandle_t mutex;

    AsyncWebSocket* ws = nullptr;

    static const size_t PENDING_SIZE = 4096;
    char pending[PENDING_SIZE];
    size_t pendingLen = 0;

    void addToBuffer(uint8_t c);
    void addToBuffer(const uint8_t* data, size_t len);

public:
    LogBuffer();

    // Wraps HardwareSerial initialization
    void begin(unsigned long baud);

    // Called after web server is set up
    void setWebSocket(AsyncWebSocket* websocket);

    // Print overrides — intercepts all print/println/printf
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t size) override;

    // Forward Serial input methods (used by calibration code)
    int available() { return HWSerial.available(); }
    int read() { return HWSerial.read(); }
    int peek() { return HWSerial.peek(); }
    String readStringUntil(char terminator) {
        return HWSerial.readStringUntil(terminator);
    }

    // Send buffered history to a new WebSocket client
    void sendHistory(uint32_t clientId);

    // Send any pending WS data — call frequently (e.g. every 50ms)
    void flush();

    // Ping all connected WS clients (detects half-open connections)
    void ping();

    // Call periodically to clean up dead WS clients
    void cleanup();
};

extern LogBuffer Logger;

// Redirect Serial to Logger for all source files that include this header.
// Library code won't include this header (build_src_flags only), so libraries
// keep using the real Serial object.
#define Serial Logger

#endif // LOG_BUFFER_H