#pragma once

#include <cstdint>
#include <string>

namespace animiq::osc {

class IOscEndpoint {
  public:
    virtual ~IOscEndpoint() = default;
    virtual bool Bind(std::uint16_t port) = 0;
    virtual bool Publish(const std::string& address, float value) = 0;
    virtual void Close() = 0;
};

}  // namespace animiq::osc

