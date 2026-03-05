#include "vsfclone/stream/spout_sender_stub.h"

namespace vsfclone::stream {

bool SpoutSenderStub::Start(const StreamConfig& cfg) {
    config_ = cfg;
    active_ = true;
    return true;
}

void SpoutSenderStub::SubmitFrame(const void* bgra_pixels, std::uint32_t bytes) {
    (void)bgra_pixels;
    (void)bytes;
    // Intentionally no-op until Spout2 SDK is wired.
}

bool SpoutSenderStub::WantsGpuTextureSubmit() const {
    return false;
}

bool SpoutSenderStub::SubmitFrameTexture(void* d3d11_device, void* d3d11_texture) {
    (void)d3d11_device;
    (void)d3d11_texture;
    return false;
}

const char* SpoutSenderStub::ActiveBackendName() const {
    return "stub";
}

std::uint64_t SpoutSenderStub::FallbackCount() const {
    return 0U;
}

void SpoutSenderStub::Stop() {
    active_ = false;
}

}  // namespace vsfclone::stream
