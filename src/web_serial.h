#ifndef WEB_SERIAL_H
#define WEB_SERIAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebSerialClass : public Print {
private:
    AsyncWebSocket* ws;
    bool enabled;
    
    // Circular buffer for history
    static const size_t BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    size_t head;
    size_t tail;

    void addToBuffer(const uint8_t* buffer, size_t size);
    void addToBuffer(uint8_t c);

public:
    WebSerialClass();
    
    // Initialize the WebSocket
    void begin(AsyncWebSocket* webSocket);
    
    // Toggle logging via web interface
    void setEnabled(bool enable);
    bool isEnabled() const { return enabled; }

    // Send full history to newly connected clients
    void sendHistory(uint32_t clientId);

    // Print class overrides
    virtual size_t write(uint8_t c) override;
    virtual size_t write(const uint8_t *buffer, size_t size) override;
};

extern WebSerialClass WebSerial;

#endif // WEB_SERIAL_H
