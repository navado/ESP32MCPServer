#pragma once
// esp_random.h stub for native builds.

#include <stdint.h>
#include <cstdlib>

inline uint32_t esp_random() { return (uint32_t)rand(); }
