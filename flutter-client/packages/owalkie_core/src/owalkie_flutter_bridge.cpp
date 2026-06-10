#include "owalkie_flutter_bridge.h"

#include "owalkie_core.h"

extern "C" {

FFI_PLUGIN_EXPORT const char* owalkie_flutter_core_version(void) {
    return owalkie_version_string();
}

FFI_PLUGIN_EXPORT int32_t owalkie_flutter_protocol_version(void) {
    return static_cast<int32_t>(OWALKIE_PROTOCOL_VERSION);
}

}