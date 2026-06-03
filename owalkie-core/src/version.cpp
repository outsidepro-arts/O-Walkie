#include "owalkie_core.h"

namespace {

constexpr int kMajor = 0;
constexpr int kMinor = 1;
constexpr char kVersionString[] = "0.1.0";

} // namespace

const char* owalkie_version_string() {
    return kVersionString;
}

int owalkie_version_major() {
    return kMajor;
}

int owalkie_version_minor() {
    return kMinor;
}
