#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <functional>
#include <mutex>
#include <vector>

class uLogger {
public:
    static const size_t MAX_NAME_LENGTH = 64;
    static const size_t MAX_DATA_LENGTH = 128;

    struct Record {
        uint64_t timestamp;
        char     name[MAX_NAME_LENGTH];
        uint16_t dataSize;
        uint8_t  data[MAX_DATA_LENGTH];

        Record() : timestamp(0), dataSize(0) {
            memset(name, 0, MAX_NAME_LENGTH);
            memset(data, 0, MAX_DATA_LENGTH);
        }
    };

    uLogger();
    ~uLogger();

    bool begin(const char* logFile = "/metrics.log");
    void end();

    bool   logMetric(const char* name, const void* data, size_t dataSize);

    size_t queryMetrics(const char* name, uint64_t startTime,
                        std::vector<Record>& records);

    size_t queryMetrics(std::function<bool(const Record&)> callback,
                        const char* name = "", uint64_t startTime = 0);

    size_t getRecordCount();

    bool clear();
    bool compact(uint64_t maxAge);

private:
    static const size_t MAX_FILE_SIZE = 1024 * 1024; // 1 MB

    File        logFile_;
    String      logFilePath_;
    std::mutex  mutex_;
    bool        initialized_;

    bool openLog(const char* mode);
    void closeLog();
    bool writeRecord(const Record& record);
    bool readRecord(Record& record);
    bool seekToStart();

    // Rotate with mutex already held — does NOT call clear()
    bool rotateLogImpl();
};
