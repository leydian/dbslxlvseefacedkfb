#include "vsfclone/osc/osc_endpoint_stub.h"

namespace vsfclone::osc {

bool OscEndpointStub::Bind(std::uint16_t port) {
    port_ = port;
    bound_ = true;
    return true;
}

bool OscEndpointStub::Publish(const std::string& address, float value) {
    (void)address;
    (void)value;
    return bound_;
}

void OscEndpointStub::Close() {
    bound_ = false;
    port_ = 0;
}

}  // namespace vsfclone::osc

