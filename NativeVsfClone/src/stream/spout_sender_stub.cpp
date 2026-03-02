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

void SpoutSenderStub::Stop() {
    active_ = false;
}

}  // namespace vsfclone::stream

