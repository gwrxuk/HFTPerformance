/**
 * @file utils.cpp
 * @brief Core utility implementations
 */

#include <cstdio>
#include <cstdlib>
#include "types.hpp"

namespace hft {

// Version information
const char* get_version() {
    return "1.0.0";
}

const char* get_build_info() {
    #ifdef NDEBUG
        return "Release";
    #else
        return "Debug";
    #endif
}

} // namespace hft

