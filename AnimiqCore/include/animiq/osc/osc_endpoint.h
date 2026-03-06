#pragma once

#include <cstdint>
#include <string>

#include "animiq/osc/i_osc_endpoint.h"

namespace animiq::osc {

class OscEndpoint final : public IOscEndpoint {
  public:
    bool Bind(std::uint16_t port) override;
    bool Publish(const std::string& address, float value) override;
    void Close() override;

    bool SetDestination(const std::string& host_port);
    [[nodiscard]] bool IsBound() const;

  private:
    bool bound_ = false;
    std::uint16_t port_ = 0;
    std::string remote_host_ = "127.0.0.1";
    std::uint16_t remote_port_ = 39539;
    std::uintptr_t socket_handle_ = static_cast<std::uintptr_t>(~0ULL);
    bool wsa_started_ = false;
};

}  // namespace animiq::osc
