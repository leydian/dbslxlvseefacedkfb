#pragma once

#include <cstdint>
#include <string>

#include "vsfclone/stream/i_streaming_output.h"

namespace vsfclone::stream {

class SpoutSender final : public IStreamingOutput {
  public:
    enum class BackendKind : std::uint32_t {
        Inactive = 0,
        LegacySharedMemory = 1,
        Spout2Gpu = 2
    };

    bool Start(const StreamConfig& cfg) override;
    void SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) override;
    bool WantsGpuTextureSubmit() const override;
    bool SubmitFrameTexture(void* d3d11_device, void* d3d11_texture) override;
    const char* ActiveBackendName() const override;
    std::uint64_t FallbackCount() const override;
    void Stop() override;

    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] std::uint64_t FrameCount() const;
    [[nodiscard]] bool IsStrictMode() const;
    [[nodiscard]] BackendKind ActiveBackendKind() const;
    [[nodiscard]] const std::string& LastErrorCode() const;

  private:
    enum class BackendMode {
        None = 0,
        Spout2Gpu = 1,
        LegacySharedMemory = 2
    };

    bool StartLegacySharedMemory(const StreamConfig& cfg);
    void SubmitLegacySharedMemory(const void* bgra_pixels, std::uint32_t bytes);
    void StopLegacySharedMemory();
    bool TryInitSpout2(void* d3d11_device, void* d3d11_texture);
    bool TrySendSpout2(void* d3d11_device, void* d3d11_texture);
    void StopSpout2();
    bool ResolveStrictMode() const;

    bool active_ = false;
    bool strict_mode_ = false;
    StreamConfig config_ {};
    void* mapping_ = nullptr;
    std::string mapping_name_;
    std::uint64_t frame_counter_ = 0;
    std::uint64_t fallback_count_ = 0;
    std::string last_error_code_;
    BackendMode backend_mode_ = BackendMode::None;
};

}  // namespace vsfclone::stream
