#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <cstring>

// ---------------------------------------------------------------------------
// MockFile
//
// File data is stored in a shared_ptr<vector<uint8_t>> so that a copy of a
// MockFile returned from MockLittleFS::open() still writes/reads through the
// same underlying buffer.  This mirrors real filesystem semantics where the
// file contents are persisted independently of any particular handle object.
// ---------------------------------------------------------------------------
class MockFile {
public:
    MockFile()
        : data_(std::make_shared<std::vector<uint8_t>>()),
          position_(0), mode_(""), open_(false) {}

    bool isOpen() const { return open_; }

    // operator bool so code like `if (file) { ... }` works
    explicit operator bool() const { return open_; }

    size_t write(const uint8_t* buf, size_t size) {
        if (!open_ || !canWrite()) return 0;
        if (mode_ == "a" || mode_ == "a+") position_ = data_->size();
        if (position_ + size > data_->size()) data_->resize(position_ + size);
        std::copy(buf, buf + size, data_->begin() + position_);
        position_ += size;
        return size;
    }

    size_t write(uint8_t b) { return write(&b, 1); }

    size_t read(uint8_t* buf, size_t size) {
        if (!open_ || !canRead()) return 0;
        size_t avail = std::min(size, data_->size() - position_);
        if (avail > 0) {
            std::copy(data_->begin() + position_,
                      data_->begin() + position_ + avail, buf);
            position_ += avail;
        }
        return avail;
    }

    // Single-byte read (returns -1 on failure)
    int read() {
        if (!open_ || position_ >= data_->size()) return -1;
        return static_cast<int>((*data_)[position_++]);
    }

    bool seek(size_t pos) {
        if (!open_ || pos > data_->size()) return false;
        position_ = pos;
        return true;
    }

    size_t size() const { return data_->size(); }
    bool available() const { return position_ < data_->size(); }
    void close() { open_ = false; }

    // For tests that need direct access to the raw data
    std::vector<uint8_t>& getData() { return *data_; }
    const std::string& getMode() const { return mode_; }

private:
    friend class MockLittleFS;

    std::shared_ptr<std::vector<uint8_t>> data_;
    size_t position_;
    std::string mode_;
    bool open_;

    // Constructor used by MockLittleFS to inject shared storage
    explicit MockFile(std::shared_ptr<std::vector<uint8_t>> data)
        : data_(data), position_(0), mode_(""), open_(false) {}

    bool canWrite() const {
        return mode_ == "w" || mode_ == "w+" ||
               mode_ == "a" || mode_ == "a+" ||
               mode_ == "r+";
    }
    bool canRead() const {
        return mode_ == "r"  || mode_ == "r+" ||
               mode_ == "w+" || mode_ == "a+";
    }

    void openInMode(const char* m) {
        mode_ = m;
        open_ = true;
        if (mode_ == "w" || mode_ == "w+") {
            data_->clear();
            position_ = 0;
        } else if (mode_ == "a" || mode_ == "a+") {
            position_ = data_->size();
        } else {
            position_ = 0;
        }
    }
};

// ---------------------------------------------------------------------------
// MockLittleFS
// ---------------------------------------------------------------------------
class MockLittleFS {
public:
    MockLittleFS() : mounted_(false) {}

    bool begin(bool /*formatOnFail*/ = false) {
        mounted_ = true;
        return true;
    }

    void end() {
        files_.clear();
        directories_.clear();
        mounted_ = false;
    }

    MockFile open(const char* path, const char* mode) {
        if (!mounted_) return MockFile();

        std::shared_ptr<std::vector<uint8_t>> data;

        if (strcmp(mode, "r") == 0 || strcmp(mode, "r+") == 0) {
            auto it = files_.find(path);
            if (it == files_.end()) return MockFile(); // file not found
            data = it->second;
        } else if (strcmp(mode, "w") == 0 || strcmp(mode, "w+") == 0) {
            // Create or replace
            data = std::make_shared<std::vector<uint8_t>>();
            files_[path] = data;
        } else {
            // "a" / "a+": append to existing, or create new
            auto it = files_.find(path);
            if (it == files_.end()) {
                data = std::make_shared<std::vector<uint8_t>>();
                files_[path] = data;
            } else {
                data = it->second;
            }
        }

        MockFile f(data);
        f.openInMode(mode);
        return f;
    }

    bool exists(const char* path) const {
        return files_.find(path) != files_.end();
    }

    bool remove(const char* path) {
        return files_.erase(path) > 0;
    }

    bool rename(const char* from, const char* to) {
        auto it = files_.find(from);
        if (it == files_.end()) return false;
        files_[to] = it->second;
        files_.erase(it);
        return true;
    }

    bool mkdir(const char* path) {
        directories_.insert(path);
        return true;
    }

    bool rmdir(const char* path) {
        return directories_.erase(path) > 0;
    }

    // Testing helpers
    void reset() {
        files_.clear();
        directories_.clear();
        mounted_ = false;
    }

    size_t fileCount() const { return files_.size(); }
    bool isMounted() const { return mounted_; }

    MockFile* getFile(const char* path) {
        // Not meaningful with shared_ptr design; kept for API compatibility
        (void)path;
        return nullptr;
    }

private:
    std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> files_;
    std::set<std::string> directories_;
    bool mounted_;
};

// Global instance used by tests
extern MockLittleFS MockFS;

// Redirect LittleFS to the mock in test builds
#define LittleFS MockFS
