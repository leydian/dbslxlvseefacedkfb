#pragma once

#include "animiq/stream/i_streaming_output.h"

namespace animiq::stream {

class SpoutSenderStub final : public IStreamingOutput {
  public:
    bool Start(const StreamConfig& cfg) override;
    void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) override;
    bool WantsGpuTextureSubmit() const override;
    bool SubmitFrameTexture(void* d3d11_device, void* d3d11_texture) override;
    const char* ActiveBackendName() const override;
    std::uint64_t FallbackCount() const override;
    void Stop() override;

  private:
    bool active_ = false;
    StreamConfig config_ {};
};

}  // namespace animiq::stream
