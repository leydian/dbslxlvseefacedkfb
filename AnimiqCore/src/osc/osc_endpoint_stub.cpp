#include "animiq/osc/osc_endpoint_stub.h"

namespace animiq::osc {

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

}  // namespace animiq::osc

