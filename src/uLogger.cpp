#include "uLogger.h"

uLogger::uLogger() : initialized_(false) {}

uLogger::~uLogger() {
    end();
}

bool uLogger::begin(const char* logFile) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) return true;

    logFilePath_ = logFile;

    // Try to open existing log file; create it if absent.
    if (!openLog("r+")) {
        if (!openLog("w+")) {
            log_e("Failed to create log file: %s", logFile);
            return false;
        }
    }
    closeLog();

    initialized_ = true;
    return true;
}

void uLogger::end() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLog();
    initialized_ = false;
}

bool uLogger::logMetric(const char* name, const void* data, size_t dataSize) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !name || !data || dataSize > MAX_DATA_LENGTH) {
        return false;
    }

    Record record;
    record.timestamp = millis();
    strncpy(record.name, name, MAX_NAME_LENGTH - 1);
    record.name[MAX_NAME_LENGTH - 1] = '\0';
    record.dataSize = static_cast<uint16_t>(dataSize);
    memcpy(record.data, data, dataSize);

    if (!openLog("a+")) return false;

    bool ok = writeRecord(record);

    // Rotate when the file grows too large (no mutex re-acquisition needed
    // because we are already inside the lock).
    if (logFile_.size() >= MAX_FILE_SIZE) {
        rotateLogImpl();
    }

    closeLog();
    return ok;
}

size_t uLogger::queryMetrics(const char* name, uint64_t startTime,
                             std::vector<Record>& records) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !openLog("r")) return 0;

    size_t count = 0;
    Record record;
    while (readRecord(record)) {
        if (record.timestamp >= startTime &&
            (name[0] == '\0' || strcmp(record.name, name) == 0)) {
            records.push_back(record);
            ++count;
        }
    }

    closeLog();
    return count;
}

size_t uLogger::queryMetrics(std::function<bool(const Record&)> callback,
                             const char* name, uint64_t startTime) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !openLog("r")) return 0;

    size_t count = 0;
    Record record;
    while (readRecord(record)) {
        if (record.timestamp >= startTime &&
            (name[0] == '\0' || strcmp(record.name, name) == 0)) {
            if (!callback(record)) break;
            ++count;
        }
    }

    closeLog();
    return count;
}

size_t uLogger::getRecordCount() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !openLog("r")) return 0;

    size_t count = 0;
    Record record;
    while (readRecord(record)) ++count;

    closeLog();
    return count;
}

bool uLogger::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLog();
    return LittleFS.remove(logFilePath_.c_str());
}

bool uLogger::compact(uint64_t maxAge) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) return false;

    String tempPath = logFilePath_ + ".tmp";
    File tempFile   = LittleFS.open(tempPath.c_str(), "w+");
    if (!tempFile) return false;

    if (!openLog("r")) {
        tempFile.close();
        LittleFS.remove(tempPath.c_str());
        return false;
    }

    uint64_t cutoff = millis() - maxAge;
    Record record;
    while (readRecord(record)) {
        if (record.timestamp >= cutoff) {
            size_t nameLen  = strlen(record.name) + 1;
            size_t recSize  = sizeof(record.timestamp) + sizeof(record.dataSize)
                            + nameLen + record.dataSize;
            if (tempFile.write(reinterpret_cast<uint8_t*>(&record), recSize) != recSize) {
                closeLog();
                tempFile.close();
                LittleFS.remove(tempPath.c_str());
                return false;
            }
        }
    }

    closeLog();
    tempFile.close();

    LittleFS.remove(logFilePath_.c_str());
    LittleFS.rename(tempPath.c_str(), logFilePath_.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers (all called with mutex_ already held)
// ---------------------------------------------------------------------------

bool uLogger::openLog(const char* mode) {
    if (logFile_) return true; // already open
    logFile_ = LittleFS.open(logFilePath_.c_str(), mode);
    return static_cast<bool>(logFile_);
}

void uLogger::closeLog() {
    if (logFile_) logFile_.close();
}

bool uLogger::writeRecord(const Record& record) {
    // Variable-length layout: [timestamp][dataSize][name NUL-terminated][data]
    size_t nameLen = strlen(record.name) + 1;

    if (logFile_.write(reinterpret_cast<const uint8_t*>(&record.timestamp),
                       sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (logFile_.write(reinterpret_cast<const uint8_t*>(&record.dataSize),
                       sizeof(record.dataSize)) != sizeof(record.dataSize)) return false;
    if (logFile_.write(reinterpret_cast<const uint8_t*>(record.name),
                       nameLen) != nameLen) return false;
    if (logFile_.write(record.data, record.dataSize) != record.dataSize) return false;
    return true;
}

bool uLogger::readRecord(Record& record) {
    if (!logFile_.available()) return false;

    if (logFile_.read(reinterpret_cast<uint8_t*>(&record.timestamp),
                      sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (logFile_.read(reinterpret_cast<uint8_t*>(&record.dataSize),
                      sizeof(record.dataSize)) != sizeof(record.dataSize)) return false;

    // Read NUL-terminated name
    size_t nameLen = 0;
    while (nameLen < MAX_NAME_LENGTH - 1) {
        int c = logFile_.read();
        if (c < 0) return false;
        if (c == '\0') break;
        record.name[nameLen++] = static_cast<char>(c);
    }
    record.name[nameLen] = '\0';

    if (record.dataSize > MAX_DATA_LENGTH) return false;
    if (logFile_.read(record.data, record.dataSize) != record.dataSize) return false;

    return true;
}

bool uLogger::seekToStart() {
    return logFile_.seek(0);
}

// ---------------------------------------------------------------------------
// rotateLogImpl
//
// Retains only records that fit within MAX_FILE_SIZE / 2 (most-recent wins).
// Called with mutex_ held and the log file open — must NOT call clear() or
// any other public method that would re-acquire the mutex.
// ---------------------------------------------------------------------------
bool uLogger::rotateLogImpl() {
    String tempPath = logFilePath_ + ".rot";

    // Collect all records; maintain a sliding window sized at MAX_FILE_SIZE/2.
    std::vector<Record> keep;
    size_t kept = 0;

    seekToStart();
    Record record;
    while (readRecord(record)) {
        size_t recSize = sizeof(record.timestamp) + sizeof(record.dataSize)
                       + strlen(record.name) + 1 + record.dataSize;
        keep.push_back(record);
        kept += recSize;

        while (kept > MAX_FILE_SIZE / 2 && !keep.empty()) {
            size_t front = sizeof(keep.front().timestamp)
                         + sizeof(keep.front().dataSize)
                         + strlen(keep.front().name) + 1
                         + keep.front().dataSize;
            kept -= front;
            keep.erase(keep.begin());
        }
    }

    closeLog();

    // Write the kept records to a temp file
    File tmp = LittleFS.open(tempPath.c_str(), "w+");
    if (!tmp) {
        openLog("a+"); // re-open for continued appends
        return false;
    }

    for (const auto& rec : keep) {
        size_t nameLen = strlen(rec.name) + 1;
        tmp.write(reinterpret_cast<const uint8_t*>(&rec.timestamp), sizeof(rec.timestamp));
        tmp.write(reinterpret_cast<const uint8_t*>(&rec.dataSize),  sizeof(rec.dataSize));
        tmp.write(reinterpret_cast<const uint8_t*>(rec.name),       nameLen);
        tmp.write(rec.data, rec.dataSize);
    }
    tmp.close();

    // Atomically replace the original log
    LittleFS.remove(logFilePath_.c_str());
    LittleFS.rename(tempPath.c_str(), logFilePath_.c_str());

    // Re-open for continued appends
    openLog("a+");
    return true;
}
