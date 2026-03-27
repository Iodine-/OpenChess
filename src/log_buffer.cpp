#include <Arduino.h>
// Define HWSerial BEFORE including log_buffer.h so the reference
// binds to the real Serial object (before the macro replaces it).
HardwareSerial& HWSerial = Serial;

#include "log_buffer.h"

LogBuffer Logger;

LogBuffer::LogBuffer() {
    mutex = xSemaphoreCreateMutex();
}

void LogBuffer::begin(unsigned long baud) {
    HWSerial.begin(baud);
}

void LogBuffer::setWebSocket(AsyncWebSocket* websocket) {
    ws = websocket;
}

void LogBuffer::addToBuffer(uint8_t c) {
    // CALLER MUST HOLD MUTEX
    buffer[head] = c;
    head = (head + 1) % BUFFER_SIZE;
    if (head == tail) {
        tail = (tail + 1) % BUFFER_SIZE;
    }
}

void LogBuffer::addToBuffer(const uint8_t* data, size_t len) {
    // CALLER MUST HOLD MUTEX
    for (size_t i = 0; i < len; i++) {
        addToBuffer(data[i]);
    }
}

size_t LogBuffer::write(uint8_t c) {
    HWSerial.write(c);

    xSemaphoreTake(mutex, portMAX_DELAY);
    addToBuffer(c);
    if (pendingLen < PENDING_SIZE) {
        pending[pendingLen++] = c;
    }
    xSemaphoreGive(mutex);

    return 1;
}

size_t LogBuffer::write(const uint8_t* buf, size_t size) {
    HWSerial.write(buf, size);

    xSemaphoreTake(mutex, portMAX_DELAY);
    addToBuffer(buf, size);
    size_t canAdd = min(size, PENDING_SIZE - pendingLen);
    if (canAdd > 0) {
        memcpy(pending + pendingLen, buf, canAdd);
        pendingLen += canAdd;
    }
    xSemaphoreGive(mutex);

    return size;
}

void LogBuffer::sendHistory(uint32_t clientId) {
    xSemaphoreTake(mutex, portMAX_DELAY);

    if (head == tail) {
        xSemaphoreGive(mutex);
        return;
    }

    // Send in at most 2 chunks — zero allocations
    if (head > tail) {
        ws->text(clientId, &buffer[tail], head - tail);
    } else {
        ws->text(clientId, &buffer[tail], BUFFER_SIZE - tail);
        if (head > 0) {
            ws->text(clientId, &buffer[0], head);
        }
    }

    xSemaphoreGive(mutex);
}

void LogBuffer::flush() {
    if (!ws) return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    if (pendingLen == 0) {
        xSemaphoreGive(mutex);
        return;
    }

    if (ws->count() == 0) {
        // No clients — discard so pending doesn't grow stale
        pendingLen = 0;
        xSemaphoreGive(mutex);
        return;
    }

    if (ws->availableForWriteAll()) {
        ws->textAll(pending, pendingLen);
        pendingLen = 0;
    }
    // If queue is still full, hold data and retry next tick

    xSemaphoreGive(mutex);
}

void LogBuffer::ping() {
    if (ws) ws->pingAll();
}

void LogBuffer::cleanup() {
    if (ws) ws->cleanupClients();
}