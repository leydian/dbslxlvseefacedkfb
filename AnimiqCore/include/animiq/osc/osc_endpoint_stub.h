#pragma once

#include "animiq/osc/i_osc_endpoint.h"

namespace animiq::osc {

class OscEndpointStub final : public IOscEndpoint {
  public:
    bool Bind(std::uint16_t port) override;
    bool Publish(const std::string& address, float value) override;
    void Close() override;

  private:
    bool bound_ = false;
    std::uint16_t port_ = 0;
};

}  // namespace animiq::osc

