#pragma once
// LittleFS.h stub for native builds.
// Redirects to the MockLittleFS implementation.

#include "../mock/MockLittleFS.h"

// MockLittleFS.h already contains:
//   extern MockLittleFS MockFS;
//   #define LittleFS MockFS

// Expose MockFile as the Arduino 'File' type expected by uLogger and others.
using File = MockFile;

// Provide an inline definition so any translation unit including this header
// sees the same MockFS object without multiple-definition linker errors.
inline MockLittleFS MockFS;
