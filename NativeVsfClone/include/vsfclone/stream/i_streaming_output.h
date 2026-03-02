#pragma once

#include <cstdint>
#include <string>

namespace vsfclone::stream {

struct StreamConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t fps = 60;
    std::string channel_name = "VsfClone";
};

class IStreamingOutput {
  public:
    virtual ~IStreamingOutput() = default;
    virtual bool Start(const StreamConfig& cfg) = 0;
    virtual void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) = 0;
    virtual void Stop() = 0;
};

}  // namespace vsfclone::stream

