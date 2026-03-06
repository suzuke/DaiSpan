#pragma once

#include <Arduino.h>
#include <WebServer.h>

// 流式 HTTP 響應構建器，避免大型 String 分配
class StreamingResponse {
    static constexpr size_t CHUNK_SIZE = 512;
    char buffer[CHUNK_SIZE + 1];
    size_t pos = 0;
    WebServer* server = nullptr;
    bool active = false;

public:
    void begin(WebServer* srv, const char* contentType = "text/html") {
        server = srv;
        server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        server->send(200, contentType, "");
        active = true;
        pos = 0;
    }

    void append(const char* content) {
        if (!active) return;
        size_t len = strlen(content);
        size_t i = 0;
        while (i < len) {
            size_t n = min(len - i, CHUNK_SIZE - pos);
            memcpy(buffer + pos, content + i, n);
            pos += n;
            i += n;
            if (pos >= CHUNK_SIZE) flush();
        }
    }

    void appendf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char tmp[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
        va_end(args);
        if (n > 0) append(tmp);
    }

    void flush() {
        if (!active || pos == 0) return;
        buffer[pos] = '\0';
        server->sendContent(buffer);
        pos = 0;
    }

    void finish() {
        if (!active) return;
        flush();
        server->sendContent("");
        active = false;
    }
};
