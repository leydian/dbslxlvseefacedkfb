#pragma once

#include "vsfclone/stream/i_streaming_output.h"

namespace vsfclone::stream {

class SpoutSenderStub final : public IStreamingOutput {
  public:
    bool Start(const StreamConfig& cfg) override;
    void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) override;
    void Stop() override;

  private:
    bool active_ = false;
    StreamConfig config_ {};
};

}  // namespace vsfclone::stream

