#pragma once

#include <cstdint>
#include <string>

namespace animiq::stream {

struct StreamConfig {
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint32_t fps = 60;
    std::string channel_name = "Animiq";
};

class IStreamingOutput {
  public:
    virtual ~IStreamingOutput() = default;
    virtual bool Start(const StreamConfig& cfg) = 0;
    virtual void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) = 0;
    // Optional fast path for GPU texture sharing backends (Spout2).
    virtual bool WantsGpuTextureSubmit() const { return false; }
    virtual bool SubmitFrameTexture(void* d3d11_device, void* d3d11_texture) {
        (void)d3d11_device;
        (void)d3d11_texture;
        return false;
    }
    virtual const char* ActiveBackendName() const { return "legacy-shared-memory"; }
    virtual std::uint64_t FallbackCount() const { return 0U; }
    virtual void Stop() = 0;
};

}  // namespace animiq::stream
