#include "web_serial.h"

WebSerialClass WebSerial;

WebSerialClass::WebSerialClass() : ws(nullptr), enabled(false), head(0), tail(0) {}

void WebSerialClass::begin(AsyncWebSocket* webSocket) {
    ws = webSocket;
}

void WebSerialClass::setEnabled(bool enable) {
    enabled = enable;
    if (!enabled) {
        // Clear history when disabled
        head = 0;
        tail = 0;
    }
}

void WebSerialClass::addToBuffer(uint8_t c) {
    buffer[head] = c;
    head = (head + 1) % BUFFER_SIZE;
    if (head == tail) {
        tail = (tail + 1) % BUFFER_SIZE; // Overwrite oldest
    }
}

void WebSerialClass::addToBuffer(const uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; i++) {
        addToBuffer(buf[i]);
    }
}

void WebSerialClass::sendHistory(uint32_t clientId) {
    if (!ws || !enabled) return;

    size_t len = (head >= tail) ? (head - tail) : (BUFFER_SIZE - tail + head);
    if (len == 0) return;

    // We allocate a temporary buffer to send the contiguous history
    uint8_t* tempBuf = (uint8_t*)malloc(len + 1);
    if (!tempBuf) return;

    size_t i = 0;
    size_t current = tail;
    while (current != head) {
        tempBuf[i++] = buffer[current];
        current = (current + 1) % BUFFER_SIZE;
    }
    tempBuf[len] = '\0';

    ws->text(clientId, tempBuf, len);
    free(tempBuf);
}

size_t WebSerialClass::write(uint8_t c) {
    // 1. Always output to hardware serial
    size_t ret = Serial.write(c);

    // 2. If disabled or no clients, avoid overhead
    if (!enabled || !ws || ws->count() == 0) {
        return ret;
    }

    // 3. Buffer and broadcast
    addToBuffer(c);
    
    // We send char by char if doing standard write(c), 
    // but ideally the user calls write(buffer, size) or println(string)
    // which hits the other override. Sending single chars over WS is inefficient.
    // For print("a") it will broadcast immediately.
    uint8_t buf[1] = {c};
    ws->textAll(buf, 1);

    return ret;
}

size_t WebSerialClass::write(const uint8_t *buf, size_t size) {
    // 1. Always output to hardware serial
    size_t ret = Serial.write(buf, size);

    // 2. If disabled or no clients, avoid overhead
    if (!enabled || !ws || ws->count() == 0) {
        return ret;
    }

    // 3. Buffer and broadcast
    addToBuffer(buf, size);
    ws->textAll(buf, size);

    return ret;
}
