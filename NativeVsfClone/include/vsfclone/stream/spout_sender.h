#pragma once

#include <cstdint>
#include <string>

#include "vsfclone/stream/i_streaming_output.h"

namespace vsfclone::stream {

class SpoutSender final : public IStreamingOutput {
  public:
    bool Start(const StreamConfig& cfg) override;
    void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) override;
    void Stop() override;

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] std::uint64_t FrameCount() const;

  private:
    bool active_ = false;
    StreamConfig config_ {};
    void* mapping_ = nullptr;
    std::string mapping_name_;
    std::uint64_t frame_counter_ = 0;
};

}  // namespace vsfclone::stream
