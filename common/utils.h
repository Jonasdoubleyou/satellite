#pragma once

#include <iostream>
#include <vector>
#include <span>
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <map>
#include <unordered_set>

#include "./config.h"

#define ASSURE(condition, msg) \
  if (!(condition)) {              \
    std::cerr << msg << "\n  Line: " << __LINE__ << "\n"; \
    exit(1);                       \
  }

#define UNREACHABLE ASSURE(false, "Unreachable")

#define PRINT(msg) std::cerr << msg << "\n";

#ifdef DEBUG
    #define DEV_ASSURE(condition, msg) ASSURE(condition, msg)
    #define DEV_ONLY(statement) statement
    #define DEV_PRINT(msg) PRINT(msg);
#else
    #define DEV_ASSURE(condition, msg)
    #define DEV_ONLY(statement)
    #define DEV_PRINT(msg)
#endif

auto start = std::chrono::high_resolution_clock::now();
void restartTime() { start = std::chrono::high_resolution_clock::now(); }

std::string duration() {
    auto elapsed = std::chrono::high_resolution_clock::now() - start;

    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) + "μs";
}
